#include "smart.h"
#include "safety.h"

#include <tlhelp32.h>
#include <filesystem>
#include <cwctype>
#include <algorithm>

namespace fs = std::filesystem;

namespace diskcleaner {

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool SameProcessName(const std::wstring& fileNameNoExt, const std::wstring& wanted) {
    return Lower(fileNameNoExt) == Lower(wanted);
}

std::wstring StripExe(const std::wstring& name) {
    const std::wstring suffix = L".exe";
    if (name.size() > suffix.size() &&
        Lower(name.substr(name.size() - suffix.size())) == suffix) {
        return name.substr(0, name.size() - suffix.size());
    }
    return name;
}

} // namespace

std::vector<std::wstring> GetCategoryProcessNames(CleanupCategory category) {
    switch (category) {
        case CleanupCategory::EdgeCache:
        case CleanupCategory::BrowserSWCache:
            return {L"msedge", L"chrome", L"brave", L"vivaldi", L"chromium"};
        case CleanupCategory::ChromeCache:
            return {L"chrome"};
        case CleanupCategory::FirefoxCache:
            return {L"firefox"};
        case CleanupCategory::VSCodeCache:
            return {L"Code"};
        case CleanupCategory::CursorCache:
            return {L"Cursor"};
        case CleanupCategory::WindsurfCache:
            return {L"Windsurf"};
        case CleanupCategory::TraeCache:
            return {L"Trae", L"Trae CN"};
        case CleanupCategory::QoderCache:
            return {L"Qoder CN", L"Qoder"};
        case CleanupCategory::ComateCache:
            return {L"Comate"};
        case CleanupCategory::JetBrainsCache:
        case CleanupCategory::JetBrainsAICache:
            return {L"idea64", L"pycharm64", L"rider64", L"clion64", L"webstorm64",
                    L"goland64", L"phpstorm64", L"rubymine64", L"datagrip64", L"studio64"};
        case CleanupCategory::VisualStudioCache:
        case CleanupCategory::MsBuildCache:
            return {L"devenv", L"MSBuild"};
        case CleanupCategory::TeamsCache:
            return {L"ms-teams", L"Teams"};
        case CleanupCategory::ZoomCache:
            return {L"Zoom"};
        case CleanupCategory::WeChatCache:
        case CleanupCategory::WeChatMediaCache:
            return {L"Weixin", L"WeChat", L"WeChatAppEx"};
        case CleanupCategory::QQCache:
            return {L"QQ", L"QQNT"};
        case CleanupCategory::ClaudeCodeCache:
            return {L"claude"};
        case CleanupCategory::CodexCache:
            return {L"codex"};
        case CleanupCategory::ZedCache:
            return {L"zed"};
        case CleanupCategory::WarpCache:
            return {L"warp"};
        case CleanupCategory::TerminalCache:
            return {L"WindowsTerminal"};
        case CleanupCategory::OneDriveCache:
            return {L"OneDrive"};
        case CleanupCategory::OutlookCache:
            return {L"OUTLOOK"};
        default:
            return {};
    }
}

std::wstring FindRunningProcess(const std::vector<std::wstring>& names) {
    if (names.empty()) return {};

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};

    std::wstring found;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const std::wstring name = StripExe(entry.szExeFile);
            for (const auto& wanted : names) {
                if (SameProcessName(name, wanted)) {
                    found = entry.szExeFile;
                    break;
                }
            }
            if (!found.empty()) break;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool IsServiceManagedCategory(CleanupCategory category) {
    switch (category) {
        case CleanupCategory::SearchIndex:
        case CleanupCategory::FontCache:
        case CleanupCategory::WindowsUpdate:
        case CleanupCategory::PrintSpooler:
            return true;
        default:
            return false;
    }
}

bool IsFileOlderThan(const std::wstring& path, int thresholdHours) {
    if (thresholdHours <= 0) return true;
    try {
        auto lastWrite = fs::last_write_time(fs::path(path));
        auto age = fs::file_time_type::clock::now() - lastWrite;
        return age >= std::chrono::hours(thresholdHours);
    } catch (...) {
        return true;
    }
}

int64_t GetDirectorySizeFiltered(const std::wstring& path, int thresholdHours,
                                 int64_t* fileCount, const std::atomic<bool>* cancel) {
    int64_t total = 0;
    int64_t count = 0;

    std::error_code ec;
    fs::recursive_directory_iterator it(fs::path(path),
        fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (cancel && cancel->load()) break;
        std::error_code entryEc;
        try {
            const auto& entry = *it;
            if (entry.is_symlink(entryEc)) {
                it.disable_recursion_pending();
            } else {
                const std::wstring leaf = entry.path().filename().wstring();
                if (IsProtectedDataName(leaf)) {
                    // 与清理引擎保持一致：受保护数据不计入可清理大小
                    if (entry.is_directory(entryEc)) {
                        it.disable_recursion_pending();
                    }
                } else if (entry.is_regular_file(entryEc)) {
                    std::error_code sizeEc;
                    auto size = entry.file_size(sizeEc);
                    if (!sizeEc && IsFileOlderThan(entry.path().wstring(), thresholdHours)) {
                        total += static_cast<int64_t>(size);
                        ++count;
                    }
                }
            }
        } catch (...) {}
        it.increment(ec);
    }

    if (fileCount) *fileCount = count;
    return total;
}

} // namespace diskcleaner
