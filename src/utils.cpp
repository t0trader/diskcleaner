#include "utils.h"
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <winevt.h>
#include <SrRestorePtApi.h>

#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "SrClient.lib")

namespace fs = std::filesystem;

namespace diskcleaner {

// 拼接路径，避免出现双反斜杠
static std::wstring JoinPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty()) return name;
    if (base.back() == L'\\' || base.back() == L'/') return base + name;
    return base + L"\\" + name;
}

std::vector<std::wstring> ExpandGlob(const std::wstring& pattern) {
    std::vector<std::wstring> results;
    std::wstring expanded = ExpandPath(pattern);
    if (expanded.empty()) return results;

    // 分割路径为段，保留前缀（盘符或 UNC）
    std::vector<std::wstring> segments;
    size_t pos = 0;

    if (expanded.size() >= 2 && expanded[0] == L'\\' && expanded[1] == L'\\') {
        // UNC: \\server\share\...
        size_t pos1 = expanded.find(L'\\', 2);
        if (pos1 == std::wstring::npos) return results;
        size_t pos2 = expanded.find(L'\\', pos1 + 1);
        if (pos2 == std::wstring::npos) {
            segments.push_back(expanded);
            pos = expanded.size();
        } else {
            segments.push_back(expanded.substr(0, pos2));
            pos = pos2 + 1;
        }
    } else if (expanded.size() >= 2 && expanded[1] == L':') {
        // C:\... 或 C:...
        if (expanded.size() >= 3 && expanded[2] == L'\\') {
            segments.push_back(expanded.substr(0, 3)); // "C:\"
            pos = 3;
        } else {
            segments.push_back(expanded.substr(0, 2)); // "C:"
            pos = 2;
        }
    }

    // 提取剩余段
    std::vector<std::wstring> rest;
    std::wstring cur;
    for (size_t i = pos; i < expanded.size(); i++) {
        wchar_t c = expanded[i];
        if (c == L'\\' || c == L'/') {
            if (!cur.empty()) {
                rest.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) rest.push_back(cur);

    std::vector<std::wstring> current = segments;
    if (current.empty()) {
        current.push_back(L".");
    }

    for (size_t i = 0; i < rest.size(); i++) {
        const std::wstring& seg = rest[i];
        bool isLast = (i + 1 == rest.size());
        std::vector<std::wstring> next;

        if (seg.find_first_of(L"*?") != std::wstring::npos) {
            for (const auto& base : current) {
                std::wstring searchPath = JoinPath(base, seg);
                WIN32_FIND_DATAW fd = {};
                HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
                if (hFind == INVALID_HANDLE_VALUE) continue;
                do {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                    if (!isLast && !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                    next.push_back(JoinPath(base, fd.cFileName));
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        } else {
            for (const auto& base : current) {
                std::wstring p = JoinPath(base, seg);
                DWORD attr = GetFileAttributesW(p.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES) continue;
                if (!isLast && !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
                next.push_back(p);
            }
        }
        current = std::move(next);
        if (current.empty()) break;
    }

    return current;
}

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
    // 先查询所需长度，避免长路径/长变量值被 MAX_PATH 截断
    DWORD len = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (len == 0) return L"";

    std::wstring buffer(len, L'\0');
    DWORD copied = GetEnvironmentVariableW(name.c_str(), buffer.data(), len);
    if (copied == 0 || copied >= len) return L"";
    buffer.resize(copied);
    return buffer;
}

std::wstring ExpandPath(const std::wstring& path) {
    DWORD len = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
    if (len == 0) return path;

    std::wstring buffer(len, L'\0');
    DWORD copied = ExpandEnvironmentStringsW(path.c_str(), buffer.data(), len);
    if (copied == 0 || copied > len) return path;
    buffer.resize(copied - 1);  // 去掉结尾的 '\0'
    return buffer;
}

std::wstring GetWindowsDir() {
    wchar_t buffer[MAX_PATH + 1] = {0};
    UINT len = GetWindowsDirectoryW(buffer, MAX_PATH);
    if (len == 0 || len > MAX_PATH) return L"C:\\Windows";
    return std::wstring(buffer, len);
}

std::wstring GetSystemDir() {
    wchar_t buffer[MAX_PATH + 1] = {0};
    UINT len = GetSystemDirectoryW(buffer, MAX_PATH);
    if (len == 0 || len > MAX_PATH) return L"C:\\Windows\\System32";
    return std::wstring(buffer, len);
}

std::wstring GetSystemDrive() {
    std::wstring winDir = GetWindowsDir();
    if (winDir.size() >= 2 && winDir[1] == L':') {
        return winDir.substr(0, 2) + L"\\";
    }
    return L"C:\\";
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

int64_t GetDirectorySize(const std::wstring& path, int64_t* fileCount,
                         const std::atomic<bool>* cancel) {
    int64_t totalSize = 0;
    int64_t count = 0;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(path,
                fs::directory_options::skip_permission_denied)) {
            if (cancel && cancel->load()) break;
            try {
                // 跳过符号链接/目录联接，避免循环遍历和大小重复计算
                if (entry.is_symlink()) continue;
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

// ============================================
// System command execution helpers
// ============================================

bool ExecuteSystemCommand(const std::wstring& command, const std::wstring& args,
                          DWORD timeoutMs) {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = command + L" " + args;

    BOOL result = CreateProcessW(
        nullptr,
        cmdLine.data(),  // CreateProcessW 可能就地修改命令行，必须传可写缓冲区
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
    );

    if (result) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
        if (waitResult == WAIT_TIMEOUT) {
            // 超时后强制终止，避免 UI 永久卡死
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return result != FALSE;
}

// 在当前进程令牌上启用指定特权（失败时忽略，由后续操作自行报告）
static void EnableProcessPrivilege(const wchar_t* name) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return;
    }
    LUID luid;
    if (LookupPrivilegeValueW(nullptr, name, &luid)) {
        TOKEN_PRIVILEGES tp = {};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(hToken);
}

bool ClearEventLogs() {
    // 清除安全日志需要 SeSecurityPrivilege（管理员默认未启用）
    EnableProcessPrivilege(SE_SECURITY_NAME);

    // 使用 Windows Event Log API 枚举所有通道并逐个清除，
    // 避免解析 wevtutil 命令行输出。
    EVT_HANDLE hEnum = EvtOpenChannelEnum(nullptr, 0);
    if (!hEnum) return false;

    std::vector<wchar_t> buffer(512);
    bool ok = true;
    for (;;) {
        DWORD used = 0;
        if (!EvtNextChannelPath(hEnum, static_cast<DWORD>(buffer.size()),
                                buffer.data(), &used)) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) break;
            if (err == ERROR_INSUFFICIENT_BUFFER) {
                buffer.resize(used);
                continue;
            }
            ok = false;
            break;
        }
        // 清除该通道日志（需要管理员权限；个别受保护通道会失败，忽略）
        EvtClearLog(nullptr, buffer.data(), nullptr, 0);
    }
    EvtClose(hEnum);
    return ok;
}

bool FlushDNSCache() {
    return ExecuteSystemCommand(L"ipconfig.exe", L"/flushdns");
}

bool DisableHibernate() {
    return ExecuteSystemCommand(L"powercfg.exe", L"-h off");
}

bool StopServiceByName(const std::wstring& serviceName) {
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) return false;

    SC_HANDLE hService = OpenServiceW(hScm, serviceName.c_str(), SERVICE_STOP);
    if (!hService) {
        CloseServiceHandle(hScm);
        return false;
    }

    SERVICE_STATUS ss = {};
    BOOL result = ControlService(hService, SERVICE_CONTROL_STOP, &ss);
    CloseServiceHandle(hService);
    CloseServiceHandle(hScm);
    return result != FALSE;
}

bool StopServiceAndWait(const std::wstring& serviceName, DWORD timeoutMs) {
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) return false;

    SC_HANDLE hService = OpenServiceW(hScm, serviceName.c_str(),
                                      SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hScm);
        return false;
    }

    SERVICE_STATUS ss = {};
    DWORD start = GetTickCount();

    // 已经停止则直接返回成功
    if (QueryServiceStatus(hService, &ss) && ss.dwCurrentState == SERVICE_STOPPED) {
        CloseServiceHandle(hService);
        CloseServiceHandle(hScm);
        return true;
    }

    // 发送停止请求（失败也继续轮询，服务可能已在停止过程中）
    ControlService(hService, SERVICE_CONTROL_STOP, &ss);

    // 轮询等待服务真正停止
    bool stopped = false;
    while (true) {
        if (!QueryServiceStatus(hService, &ss)) break;
        if (ss.dwCurrentState == SERVICE_STOPPED) {
            stopped = true;
            break;
        }
        if (GetTickCount() - start > timeoutMs) break;
        Sleep(100);
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hScm);
    return stopped;
}

bool StartServiceByName(const std::wstring& serviceName) {
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) return false;

    SC_HANDLE hService = OpenServiceW(hScm, serviceName.c_str(), SERVICE_START);
    if (!hService) {
        CloseServiceHandle(hScm);
        return false;
    }

    BOOL result = StartServiceW(hService, 0, nullptr);
    CloseServiceHandle(hService);
    CloseServiceHandle(hScm);
    return result != FALSE;
}

bool IsServiceRunning(const std::wstring& serviceName) {
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) return false;

    SC_HANDLE hService = OpenServiceW(hScm, serviceName.c_str(), SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hScm);
        return false;
    }

    SERVICE_STATUS ss = {};
    BOOL result = QueryServiceStatus(hService, &ss);
    bool running = result != FALSE && ss.dwCurrentState == SERVICE_RUNNING;
    CloseServiceHandle(hService);
    CloseServiceHandle(hScm);
    return running;
}

// ============================================
// Registry helpers
// ============================================

bool SetRegistryValue(const std::wstring& keyPath, const std::wstring& valueName, DWORD valueType, const BYTE* data, DWORD dataSize) {
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) return false;

    rc = RegSetValueExW(hKey, valueName.c_str(), 0, valueType, data, dataSize);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

bool GetRegistryValue(const std::wstring& keyPath, const std::wstring& valueName, std::wstring& outValue) {
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS) return false;

    wchar_t buffer[256] = {};
    DWORD bufferSize = sizeof(buffer);
    rc = RegQueryValueExW(hKey, valueName.c_str(), nullptr, nullptr,
        reinterpret_cast<LPBYTE>(buffer), &bufferSize);

    outValue = buffer;
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

// ============================================
// System Restore helpers (系统还原点保护)
// ============================================

bool IsSystemRestoreEnabled() {
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SystemRestore",
        0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS) return false;

    DWORD disableSR = 0;
    DWORD size = sizeof(disableSR);
    rc = RegQueryValueExW(hKey, L"DisableSR", nullptr, nullptr,
        reinterpret_cast<LPBYTE>(&disableSR), &size);
    RegCloseKey(hKey);

    if (rc == ERROR_SUCCESS && disableSR != 0) return false;
    return true;
}

int64_t CreateSystemRestorePoint(const std::wstring& description) {
    // 使用 BEGIN_SYSTEM_CHANGE 创建一个还原点。
    // 需要管理员权限且系统保护已启用，否则返回 0。
    RESTOREPOINTINFOW rpInfo = {};
    rpInfo.dwEventType = BEGIN_SYSTEM_CHANGE;
    rpInfo.dwRestorePtType = MODIFY_SETTINGS;
    rpInfo.llSequenceNumber = 0;
    wcsncpy_s(rpInfo.szDescription, _countof(rpInfo.szDescription),
              description.c_str(), _TRUNCATE);

    STATEMGRSTATUS status = {};
    if (SRSetRestorePointW(&rpInfo, &status)) {
        return static_cast<int64_t>(status.llSequenceNumber);
    }
    return 0;
}

bool OpenSystemRestore() {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"open";
    sei.lpFile = L"rstrui.exe";
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

} // namespace diskcleaner