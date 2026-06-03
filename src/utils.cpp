#include "utils.h"
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

namespace diskcleaner {

std::wstring FileSize::toString() const {
    return FormatBytes(bytes);
}

std::wstring FormatBytes(int64_t bytes) {
    if (bytes < 0) return L"0 B";

    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    std::wostringstream oss;
    if (unitIndex == 0) {
        oss << static_cast<int64_t>(size) << L" " << units[unitIndex];
    } else {
        oss << std::fixed << std::setprecision(2) << size << L" " << units[unitIndex];
    }
    return oss.str();
}

std::wstring GetEnvVar(const std::wstring& name) {
    wchar_t buffer[MAX_PATH + 1] = {0};
    DWORD len = GetEnvironmentVariableW(name.c_str(), buffer, MAX_PATH);
    if (len == 0 || len > MAX_PATH) return L"";
    return std::wstring(buffer, len);
}

std::wstring ExpandPath(const std::wstring& path) {
    wchar_t buffer[MAX_PATH + 1] = {0};
    DWORD len = ExpandEnvironmentStringsW(path.c_str(), buffer, MAX_PATH);
    if (len == 0 || len > MAX_PATH) return path;
    return std::wstring(buffer, len - 1);
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attrib = GetFileAttributesW(path.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES) &&
           (attrib & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileExists(const std::wstring& path) {
    DWORD attrib = GetFileAttributesW(path.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES) &&
           !(attrib & FILE_ATTRIBUTE_DIRECTORY);
}

int64_t GetDirectorySize(const std::wstring& path, int64_t* fileCount) {
    int64_t totalSize = 0;
    int64_t count = 0;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(path,
                fs::directory_options::skip_permission_denied)) {
            try {
                if (entry.is_regular_file()) {
                    totalSize += entry.file_size();
                    count++;
                }
            } catch (...) {
                // Skip files that can't be accessed
            }
        }
    } catch (...) {
        // Skip directories that can't be accessed
    }

    if (fileCount) *fileCount = count;
    return totalSize;
}

bool DeleteDirectoryContents(const std::wstring& path) {
    try {
        for (const auto& entry : fs::directory_iterator(path,
                fs::directory_options::skip_permission_denied)) {
            try {
                fs::remove_all(entry.path());
            } catch (...) {
                // Skip files that can't be deleted
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool DeleteDirectoryRecursive(const std::wstring& path) {
    try {
        return fs::remove_all(path) > 0;
    } catch (...) {
        return false;
    }
}

bool DeleteFileSafe(const std::wstring& path) {
    try {
        return fs::remove(path);
    } catch (...) {
        return false;
    }
}

bool EmptyRecycleBin(HWND hwnd) {
    HRESULT hr = SHEmptyRecycleBinW(
        hwnd,
        nullptr,
        SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND
    );
    return SUCCEEDED(hr);
}

bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(
        &ntAuth, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

bool RequestAdminPrivileges() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        return false;
    }
    ExitProcess(0);
    return true;
}

std::wstring GetLastErrorString() {
    DWORD err = GetLastError();
    if (err == 0) return L"";

    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring msg;
    if (buffer) {
        msg = std::wstring(buffer, len);
        LocalFree(buffer);
    }
    return msg;
}

} // namespace diskcleaner