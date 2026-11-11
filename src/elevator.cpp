#include "elevator.h"
#include <wtsapi32.h>
#include <sddl.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace diskcleaner {

// TrustedInstaller service SID (well-known)
static const wchar_t* TRUSTED_INSTALLER_SERVICE = L"TrustedInstaller";
static const wchar_t* TRUSTED_INSTALLER_SID     = L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";

// 获取当前可执行文件完整路径（支持长路径）
static std::wstring GetModulePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(),
                                       static_cast<DWORD>(buffer.size()));
        if (len == 0) return L"";
        if (len < buffer.size()) {
            buffer.resize(len);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

// ============================================
// Public API
// ============================================

std::wstring Elevator::GetCurrentLevel() {
    // TrustedInstaller 令牌的 TokenUser 可能是 SYSTEM，也可能直接是 TI 服务 SID，
    // 因此先查 TI 组，再查 SYSTEM，避免把 TI 误判为 Admin。
    if (IsRunAsTrustedInstaller()) return L"TrustedInstaller";
    if (IsRunAsSystem()) return L"SYSTEM";
    return L"Admin";
}

bool Elevator::IsRunAsAdmin() {
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

bool Elevator::IsRunAsSystem() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;

    DWORD size = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &size);
    if (size == 0) { CloseHandle(hToken); return false; }

    std::vector<BYTE> buffer(size);
    if (!GetTokenInformation(hToken, TokenUser, buffer.data(), size, &size)) {
        CloseHandle(hToken);
        return false;
    }

    auto pUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
    PSID sid = pUser->User.Sid;

    CloseHandle(hToken);
    return IsWellKnownSid(sid, WinLocalSystemSid) != FALSE;
}

bool Elevator::IsRunAsTrustedInstaller() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;

    PSID tiSid = nullptr;
    if (!ConvertStringSidToSidW(TRUSTED_INSTALLER_SID, &tiSid)) {
        CloseHandle(hToken);
        return false;
    }

    bool isTI = false;

    // TI 令牌的 TokenUser 可能直接就是 TI 服务 SID
    DWORD userSize = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &userSize);
    if (userSize != 0) {
        std::vector<BYTE> userBuf(userSize);
        if (GetTokenInformation(hToken, TokenUser, userBuf.data(), userSize, &userSize)) {
            isTI = EqualSid(reinterpret_cast<PTOKEN_USER>(userBuf.data())->User.Sid, tiSid) != FALSE;
        }
    }

    // 否则检查 TokenGroups 中是否包含 TI SID
    if (!isTI) {
        DWORD groupsSize = 0;
        GetTokenInformation(hToken, TokenGroups, nullptr, 0, &groupsSize);
        if (groupsSize != 0) {
            std::vector<BYTE> groupsBuf(groupsSize);
            if (GetTokenInformation(hToken, TokenGroups, groupsBuf.data(), groupsSize, &groupsSize)) {
                auto groups = reinterpret_cast<PTOKEN_GROUPS>(groupsBuf.data());
                for (DWORD i = 0; i < groups->GroupCount; i++) {
                    if (EqualSid(groups->Groups[i].Sid, tiSid)) {
                        isTI = true;
                        break;
                    }
                }
            }
        }
    }

    LocalFree(tiSid);
    CloseHandle(hToken);
    return isTI;
}

bool Elevator::RequestAdminPrivileges() {
    std::wstring exePath = GetModulePath();
    if (exePath.empty()) return false;

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = exePath.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) return false;
    ExitProcess(0);
    return true;
}

bool Elevator::EnablePrivilege(const std::wstring& privilegeName) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValueW(nullptr, privilegeName.c_str(),
        &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return false;
    }

    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);
    return result != FALSE;
}

bool Elevator::ElevateTo(ElevateLevel level, std::wstring* err,
                         const std::wstring& extraArgs) {
    // Check if already at target level
    std::wstring current = GetCurrentLevel();

    // 新实例始终带 --elevated，附加参数用于恢复窗口位置等
    std::wstring args = L"--elevated";
    if (!extraArgs.empty()) {
        args += L" " + extraArgs;
    }

    switch (level) {
        case ElevateLevel::Admin:
            return IsRunAsAdmin();

        case ElevateLevel::System:
            if (current == L"SYSTEM") return true;
            return RunAsSystem(GetModulePath(), args, err);

        case ElevateLevel::TrustedInstaller:
            if (current == L"TrustedInstaller") return true;
            return RunAsTrustedInstaller(GetModulePath(), args, err);
    }
    return false;
}

// ============================================
// Private helpers
// ============================================

bool Elevator::EnableSeDebugPrivilege(DWORD& sessionId, std::wstring* err) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        if (err) *err = L"OpenProcessToken failed";
        return false;
    }
    DWORD len = 0;
    GetTokenInformation(hToken, TokenSessionId, &sessionId, sizeof(DWORD), &len);

    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        if (err) *err = L"LookupPrivilegeValue(SE_DEBUG) failed";
        return false;
    }
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    // AdjustTokenPrivileges 即使返回 TRUE 也可能只启用部分权限，
    // 必须先清零 LastError 才能正确判断。
    SetLastError(ERROR_SUCCESS);
    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    bool ok = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    if (!ok && err) *err = L"AdjustTokenPrivileges(SE_DEBUG) failed - not all assigned";
    return ok;
}

DWORD Elevator::LookupWinlogon(DWORD sessionId, std::wstring* err) {
    PWTS_PROCESS_INFOW pi = nullptr;
    DWORD count = 0;
    if (!WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pi, &count) || !pi) {
        if (err) *err = L"WTSEnumerateProcessesW failed";
        return 0;
    }
    DWORD pid = 0;
    for (DWORD i = 0; i < count; i++) {
        if (pi[i].SessionId == sessionId &&
            _wcsicmp(pi[i].pProcessName, L"winlogon.exe") == 0 &&
            pi[i].pUserSid != nullptr &&
            IsWellKnownSid(pi[i].pUserSid, WinLocalSystemSid)) {
            pid = pi[i].ProcessId;
            break;
        }
    }
    WTSFreeMemory(pi);
    if (pid == 0 && err) *err = L"winlogon.exe not found in current session";
    return pid;
}

void Elevator::EnableOnePrivilege(HANDLE hToken, const wchar_t* name) {
    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, name, &luid)) return;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
}

void Elevator::EnableAllPrivileges(HANDLE hToken) {
    DWORD len = 0;
    GetTokenInformation(hToken, TokenPrivileges, nullptr, 0, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return;
    auto privs = reinterpret_cast<PTOKEN_PRIVILEGES>(HeapAlloc(GetProcessHeap(), 0, len));
    if (!privs) return;
    if (GetTokenInformation(hToken, TokenPrivileges, privs, len, &len)) {
        for (DWORD i = 0; i < privs->PrivilegeCount; i++)
            privs->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, privs, 0, nullptr, nullptr);
    }
    HeapFree(GetProcessHeap(), 0, privs);
}

bool Elevator::StartTIService(DWORD& pid, std::wstring* err) {
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) { if (err) *err = L"OpenSCManager failed"; return false; }

    SC_HANDLE hSvc = OpenServiceW(hScm, TRUSTED_INSTALLER_SERVICE,
        SERVICE_QUERY_STATUS | SERVICE_START);
    if (!hSvc) {
        CloseServiceHandle(hScm);
        if (err) *err = L"OpenService TrustedInstaller failed";
        return false;
    }

    SERVICE_STATUS_PROCESS ssp = {};
    DWORD needed = 0;
    bool startCalled = false;
    const ULONGLONG startTick = GetTickCount64();
    const ULONGLONG timeoutMs = 20000;

    for (;;) {
        if (!QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed))
            break;

        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            pid = ssp.dwProcessId;
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hScm);
            return true;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            if (!startCalled) {
                StartServiceW(hSvc, 0, nullptr);
                startCalled = true;
            }
            // 服务启动失败会一直停留在 STOPPED，必须靠超时退出，
            // 否则此处会变成忙等死循环。
        } else if (ssp.dwCurrentState != SERVICE_STOP_PENDING &&
                   ssp.dwCurrentState != SERVICE_START_PENDING) {
            break;
        }

        if (GetTickCount64() - startTick > timeoutMs) break;
        Sleep(100);
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    if (err) *err = L"TrustedInstaller service failed to start";
    return false;
}

bool Elevator::LaunchProcess(const std::wstring& exePath, const std::wstring& args,
                             HANDLE hPrimaryToken, std::wstring* err) {
    if (exePath.empty()) {
        if (err) *err = L"Empty executable path";
        return false;
    }

    std::wstring exeDir = std::filesystem::path(exePath).parent_path().wstring();
    std::wstring cmdLine = L"\"" + exePath + L"\"";
    if (!args.empty()) cmdLine += L" " + args;

    STARTUPINFOW si = { sizeof(si) };
    si.lpDesktop = const_cast<LPWSTR>(L"WinSta0\\Default");
    PROCESS_INFORMATION pi = {};

    // GUI 程序不能使用 CREATE_NEW_CONSOLE，否则会额外弹出一个控制台窗口
    BOOL ok = ::CreateProcessAsUserW(hPrimaryToken, exePath.c_str(), cmdLine.data(),
        nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
        exeDir.empty() ? nullptr : exeDir.c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else if (err) {
        *err = L"CreateProcessAsUserW failed, error: " + std::to_wstring(GetLastError());
    }
    return ok != FALSE;
}

// ============================================
// SYSTEM elevation (Privexec execute_with_system)
// ============================================

bool Elevator::RunAsSystem(const std::wstring& exePath, const std::wstring& args,
                           std::wstring* err) {
    DWORD sessionId = 0;
    if (!EnableSeDebugPrivilege(sessionId, err)) return false;

    DWORD sysPid = LookupWinlogon(sessionId, err);
    if (sysPid == 0) return false;

    // Step 1: Open winlogon and duplicate as impersonation token
    HANDLE hProc = OpenProcess(MAXIMUM_ALLOWED, FALSE, sysPid);
    if (!hProc) { if (err) *err = L"OpenProcess(winlogon) failed"; return false; }

    HANDLE hExistingToken = nullptr;
    if (!OpenProcessToken(hProc, MAXIMUM_ALLOWED, &hExistingToken)) {
        CloseHandle(hProc);
        if (err) *err = L"OpenProcessToken(winlogon) failed";
        return false;
    }
    CloseHandle(hProc);

    // Step 2: Duplicate as impersonation
    HANDLE hImp = nullptr;
    if (!DuplicateTokenEx(hExistingToken, MAXIMUM_ALLOWED, nullptr,
        SecurityImpersonation, TokenImpersonation, &hImp)) {
        CloseHandle(hExistingToken);
        if (err) *err = L"Dup1 failed";
        return false;
    }
    CloseHandle(hExistingToken);

    // Step 3: Enable specific privileges and impersonate
    EnableOnePrivilege(hImp, SE_TCB_NAME);
    EnableOnePrivilege(hImp, SE_ASSIGNPRIMARYTOKEN_NAME);
    EnableOnePrivilege(hImp, SE_INCREASE_QUOTA_NAME);

    if (!SetThreadToken(nullptr, hImp)) {
        CloseHandle(hImp);
        if (err) *err = L"SetThreadToken failed";
        return false;
    }

    // Step 4: Now impersonating SYSTEM — re-open winlogon token
    hProc = OpenProcess(MAXIMUM_ALLOWED, FALSE, sysPid);
    if (!hProc) { RevertToSelf(); CloseHandle(hImp); if (err) *err = L"OpenProcess2 failed"; return false; }

    HANDLE hToken2 = nullptr;
    if (!OpenProcessToken(hProc, MAXIMUM_ALLOWED, &hToken2)) {
        CloseHandle(hProc); RevertToSelf(); CloseHandle(hImp);
        if (err) *err = L"OpenProcessToken2 failed"; return false;
    }
    CloseHandle(hProc);

    // Step 5: Duplicate as PRIMARY
    HANDLE hPrimary = nullptr;
    if (!DuplicateTokenEx(hToken2, TOKEN_ALL_ACCESS, nullptr,
        SecurityImpersonation, TokenPrimary, &hPrimary)) {
        CloseHandle(hToken2); RevertToSelf(); CloseHandle(hImp);
        if (err) *err = L"Dup2 failed"; return false;
    }
    CloseHandle(hToken2);

    // Step 6: Set session ID
    SetTokenInformation(hPrimary, TokenSessionId, &sessionId, sizeof(sessionId));

    // Step 7: Launch WHILE STILL IMPERSONATING SYSTEM
    bool ok = LaunchProcess(exePath, args, hPrimary, err);

    RevertToSelf();
    CloseHandle(hImp);
    CloseHandle(hPrimary);
    return ok;
}

// ============================================
// TrustedInstaller elevation (Privexec execute_with_ti)
// ============================================

bool Elevator::RunAsTrustedInstaller(const std::wstring& exePath, const std::wstring& args,
                                     std::wstring* err) {
    DWORD sessionId = 0;
    if (!EnableSeDebugPrivilege(sessionId, err)) return false;

    DWORD sysPid = LookupWinlogon(sessionId, err);
    if (sysPid == 0) return false;

    // Step 1: Impersonate as SYSTEM with ALL privileges
    HANDLE hProc = OpenProcess(MAXIMUM_ALLOWED, FALSE, sysPid);
    if (!hProc) { if (err) *err = L"OpenProcess(winlogon) failed"; return false; }

    HANDLE hExistingToken = nullptr;
    if (!OpenProcessToken(hProc, MAXIMUM_ALLOWED, &hExistingToken)) {
        CloseHandle(hProc);
        if (err) *err = L"OpenProcessToken(winlogon) failed";
        return false;
    }
    CloseHandle(hProc);

    HANDLE hImp = nullptr;
    if (!DuplicateTokenEx(hExistingToken, MAXIMUM_ALLOWED, nullptr,
        SecurityImpersonation, TokenImpersonation, &hImp)) {
        CloseHandle(hExistingToken);
        if (err) *err = L"Dup(winlogon) failed";
        return false;
    }
    CloseHandle(hExistingToken);

    // Enable ALL privileges (Privexec: pv=nullptr means enable all)
    EnableAllPrivileges(hImp);

    if (!SetThreadToken(nullptr, hImp)) {
        CloseHandle(hImp);
        if (err) *err = L"SetThreadToken failed";
        return false;
    }

    // Step 2: Start TrustedInstaller service (now running as SYSTEM)
    DWORD tiPid = 0;
    if (!StartTIService(tiPid, err)) {
        RevertToSelf(); CloseHandle(hImp);
        return false;
    }

    // Step 3: Duplicate TI process token
    HANDLE hTiProc = OpenProcess(MAXIMUM_ALLOWED, FALSE, tiPid);
    if (!hTiProc) { RevertToSelf(); CloseHandle(hImp); if (err) *err = L"OpenProcess(TI) failed"; return false; }

    HANDLE hTiToken = nullptr;
    if (!OpenProcessToken(hTiProc, MAXIMUM_ALLOWED, &hTiToken)) {
        CloseHandle(hTiProc); RevertToSelf(); CloseHandle(hImp);
        if (err) *err = L"OpenProcessToken(TI) failed"; return false;
    }
    CloseHandle(hTiProc);

    HANDLE hPrimary = nullptr;
    if (!DuplicateTokenEx(hTiToken, MAXIMUM_ALLOWED, nullptr,
        SecurityImpersonation, TokenPrimary, &hPrimary)) {
        CloseHandle(hTiToken); RevertToSelf(); CloseHandle(hImp);
        if (err) *err = L"DuplicateTokenEx(TI) failed"; return false;
    }
    CloseHandle(hTiToken);

    // Step 4: Set session ID
    SetTokenInformation(hPrimary, TokenSessionId, &sessionId, sizeof(sessionId));

    // Step 5: Launch WHILE STILL IMPERSONATING SYSTEM
    bool ok = LaunchProcess(exePath, args, hPrimary, err);

    RevertToSelf();
    CloseHandle(hImp);
    CloseHandle(hPrimary);
    return ok;
}

} // namespace diskcleaner
