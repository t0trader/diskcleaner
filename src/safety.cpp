#include "safety.h"
#include "utils.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace diskcleaner {

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring TrimTrailingSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) {
        p.pop_back();
    }
    return p;
}

bool EndsWith(const std::wstring& s, const std::wstring& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool IsProtectedDataName(const std::wstring& leaf) {
    static const wchar_t* kNames[] = {
        // Chromium / Firefox 用户数据
        L"login data", L"login data-journal", L"login data for account",
        L"cookies", L"cookies-journal", L"network",
        L"bookmarks", L"bookmarks.bak", L"encryptedbookmarks2",
        L"preferences", L"secure preferences", L"local state",
        L"web data", L"history", L"history-journal",
        L"favicons", L"top sites", L"shortcuts",
        L"session storage", L"local storage", L"indexeddb",
        L"local extension settings", L"managed extension settings",
        L"extension state", L"extension cookies", L"sync data",
        L"sync extension settings", L"origin bound certs", L"transportsecurity",
        L"affiliation database", L"dips", L"autofillstrikedatabase",
        L"profile data", L"app data", L"web applications",
        // 开发工具配置
        L"settings.json", L"keybindings.json", L"keymap.json",
        L"auth.json", L"credentials.json", L"config.toml",
        // 邮件与密钥
        L"backup", L"backups", L"plugins", L"extensions",
    };

    const std::wstring name = Lower(leaf);
    for (const auto* n : kNames) {
        if (name == n) return true;
    }

    static const wchar_t* kExts[] = {
        L".ost", L".pst", L".kdbx", L".key", L".pem", L".pfx", L".p12",
    };
    for (const auto* ext : kExts) {
        if (EndsWith(name, ext)) return true;
    }
    return false;
}

bool PathEqualsOrUnder(const std::wstring& path, const std::wstring& root) {
    std::wstring p = Lower(TrimTrailingSlash(path));
    std::wstring r = Lower(TrimTrailingSlash(root));
    if (r.empty()) return false;
    if (p == r) return true;
    if (p.size() <= r.size()) return false;
    return p.compare(0, r.size(), r) == 0 && p[r.size()] == L'\\';
}

bool IsSafeDeleteRoot(const std::wstring& path) {
    const std::wstring p = Lower(TrimTrailingSlash(path));
    if (p.empty()) return false;

    // 盘符根 "c:" / "c:\"（去掉反斜杠后长度为 2）
    if (p.size() <= 2 && p.size() >= 1 && p.back() == L':') return false;
    if (p.size() == 3 && p[1] == L':' && p[2] == L'\\') return false;

    const std::wstring winDir = Lower(TrimTrailingSlash(GetWindowsDir()));
    const std::wstring sysDir = Lower(TrimTrailingSlash(GetSystemDir()));
    const std::wstring userProfile = Lower(TrimTrailingSlash(ExpandPath(L"%USERPROFILE%")));
    const std::wstring localAppData = Lower(TrimTrailingSlash(ExpandPath(L"%LOCALAPPDATA%")));
    const std::wstring appData = Lower(TrimTrailingSlash(ExpandPath(L"%APPDATA%")));
    const std::wstring programData = Lower(TrimTrailingSlash(ExpandPath(L"%ProgramData%")));
    const std::wstring programFiles = Lower(TrimTrailingSlash(ExpandPath(L"%ProgramFiles%")));
    const std::wstring programFilesX86 = Lower(TrimTrailingSlash(ExpandPath(L"%ProgramFiles(x86)%")));

    static const wchar_t* kDeny[] = {
        L"c:\\windows\\system32", L"c:\\windows\\syswow64", L"c:\\windows\\winsxs",
        L"c:\\windows\\installer", L"c:\\windows\\fonts", L"c:\\windows\\assembly",
        L"c:\\windows\\servicing", L"c:\\users", L"c:\\users\\public",
        L"c:\\$recycle.bin", L"c:\\system volume information",
    };

    if (p == winDir || p == sysDir || p == userProfile || p == localAppData ||
        p == appData || p == programData || p == programFiles || p == programFilesX86) {
        return false;
    }
    for (const auto* d : kDeny) {
        if (p == d) return false;
    }

    // 目录联接/符号链接一律不删
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return false;

    return true;
}

} // namespace diskcleaner
