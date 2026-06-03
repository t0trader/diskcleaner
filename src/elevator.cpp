#include "elevator.h"
#include <tlhelp32.h>
#include <sddl.h>
#include <shellapi.h>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace diskcleaner {

// TrustedInstaller service name
static const wchar_t* TRUSTED_INSTALLER_SERVICE = L"TrustedInstaller";
static const wchar_t* TEMP_SERVICE_NAME = L"DiskCleanerElevate";

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
    SID* sid = reinterpret_cast<SID*>(pUser->User.Sid);

    // SYSTEM SID: S-1-5-18
    CloseHandle(hToken);
    return (sid->SubAuthorityCount >= 1 && sid->SubAuthority[0] == SECURITY_LOCAL_SYSTEM_RID);
}

bool Elevator::IsRunAsTrustedInstaller() {
    // Check if running as TrustedInstaller by looking at the token groups
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;

    DWORD size = 0;
    GetTokenInformation(hToken, TokenGroups, nullptr, 0, &size);
    if (size == 0) { CloseHandle(hToken); return false; }

    std::vector<BYTE> buffer(size);
    if (!GetTokenInformation(hToken, TokenGroups, buffer.data(), size, &size)) {
        CloseHandle(hToken);
        return false;
    }

    auto pGroups = reinterpret_cast<PTOKEN_GROUPS>(buffer.data());

    // TrustedInstaller SID: S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464
    // Simplified check: look for the well-known TrustedInstaller SID
    LPCWSTR tiSid = L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
    PSID pTiSid = nullptr;
    ConvertStringSidToSidW(tiSid, &pTiSid);

    bool isTI = false;
    if (pTiSid) {
        for (DWORD i = 0; i < pGroups->GroupCount; i++) {
            if (EqualSid(pGroups->Groups[i].Sid, pTiSid)) {
                isTI = true;
                break;
            }
        }
        LocalFree(pTiSid);
    }

    CloseHandle(hToken);
    return isTI;
}

ElevateLevel Elevator::GetCurrentLevel() {
    if (IsRunAsTrustedInstaller()) return ElevateLevel::TrustedInstaller;
    if (IsRunAsSystem()) return ElevateLevel::System;
    if (IsRunAsAdmin()) return ElevateLevel::Admin;
    return ElevateLevel::Admin; // Default
}

bool Elevator::RequestAdminPrivileges() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
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

DWORD Elevator::FindTrustedInstallerPid() {
    // Open the TrustedInstaller service
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) return 0;

    SC_HANDLE hService = OpenServiceW(hScm, TRUSTED_INSTALLER_SERVICE, SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hScm);
        return 0;
    }

    SERVICE_STATUS_PROCESS ssp = {};
    DWORD bytesNeeded = 0;
    DWORD pid = 0;

    if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            pid = ssp.dwProcessId;
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hScm);
    return pid;
}

HANDLE Elevator::DuplicateTokenFromPid(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return nullptr;

    HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
        CloseHandle(hProcess);
        return nullptr;
    }

    HANDLE hDupToken = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    DuplicateTokenEx(hToken,
        TOKEN_ALL_ACCESS,
        &sa,
        SecurityImpersonation,
        TokenImpersonation,
        &hDupToken);

    CloseHandle(hToken);
    CloseHandle(hProcess);
    return hDupToken;
}

bool Elevator::CreateProcessAsTrustedInstaller(const std::wstring& cmdline) {
    // First, start the TrustedInstaller service if not running
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr,
        SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!hScm) return false;

    SC_HANDLE hService = OpenServiceW(hScm, TRUSTED_INSTALLER_SERVICE,
        SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hScm);
        return false;
    }

    // Start the service if not running
    SERVICE_STATUS_PROCESS ssp = {};
    DWORD bytesNeeded = 0;
    QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded);

    if (ssp.dwCurrentState != SERVICE_RUNNING) {
        if (!StartServiceW(hService, 0, nullptr)) {
            CloseServiceHandle(hService);
            CloseServiceHandle(hScm);
            return false;
        }
        // Wait for service to start
        for (int i = 0; i < 30; i++) {
            Sleep(100);
            QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded);
            if (ssp.dwCurrentState == SERVICE_RUNNING) break;
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hScm);

    if (ssp.dwProcessId == 0) return false;

    // Duplicate token from TrustedInstaller process
    HANDLE hTiToken = DuplicateTokenFromPid(ssp.dwProcessId);
    if (!hTiToken) return false;

    // Create process with TrustedInstaller token
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};

    // Get current exe path if no cmdline specified
    std::wstring cmd = cmdline;
    if (cmd.empty()) {
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        cmd = std::wstring(L"\"") + exePath + L"\" --elevated";
    }

    // Impersonate the token and create process
    if (!ImpersonateLoggedOnUser(hTiToken)) {
        CloseHandle(hTiToken);
        return false;
    }

    // Create process in the user's desktop
    DWORD creationFlags = CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;

    // We need to use CreateProcessWithTokenW for proper token assignment
    BOOL result = CreateProcessWithTokenW(
        hTiToken,
        LOGON_WITH_PROFILE,
        nullptr,
        const_cast<LPWSTR>(cmd.c_str()),
        creationFlags,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    RevertToSelf();
    CloseHandle(hTiToken);

    if (result) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }

    // Fallback: try using CreateProcessAsUser
    // This requires SE_ASSIGNPRIMARYTOKEN_NAME and SE_INCREASE_QUOTA_NAME privileges
    EnablePrivilege(L"SeAssignPrimaryTokenPrivilege");
    EnablePrivilege(L"IncreaseQuotaPrivilege");

    HANDLE hPrimaryToken = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    if (DuplicateTokenEx(hTiToken, TOKEN_ALL_ACCESS, &sa,
        SecurityIdentification, TokenPrimary, &hPrimaryToken)) {
        result = CreateProcessAsUserW(
            hPrimaryToken,
            nullptr,
            const_cast<LPWSTR>(cmd.c_str()),
            nullptr, nullptr,
            FALSE, creationFlags, nullptr, nullptr,
            &si, &pi
        );
        CloseHandle(hPrimaryToken);
    }

    if (result) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return result != FALSE;
}

bool Elevator::CreateProcessAsSystem(const std::wstring& cmdline) {
    // Find SYSTEM process (winlogon.exe, csrss.exe, or services.exe)
    DWORD systemPid = 0;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = {sizeof(pe)};
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            // Look for winlogon.exe or services.exe (runs as SYSTEM)
            if (_wcsicmp(pe.szExeFile, L"winlogon.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"services.exe") == 0) {
                systemPid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);

    if (systemPid == 0) return false;

    // Duplicate token
    HANDLE hSysToken = DuplicateTokenFromPid(systemPid);
    if (!hSysToken) return false;

    // Enable debug privilege for token manipulation
    EnablePrivilege(L"SeDebugPrivilege");
    EnablePrivilege(L"SeAssignPrimaryTokenPrivilege");
    EnablePrivilege(L"IncreaseQuotaPrivilege");

    // Create primary token
    HANDLE hPrimaryToken = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    if (!DuplicateTokenEx(hSysToken, TOKEN_ALL_ACCESS, &sa,
        SecurityIdentification, TokenPrimary, &hPrimaryToken)) {
        CloseHandle(hSysToken);
        return false;
    }

    // Get current exe path if no cmdline specified
    std::wstring cmd = cmdline;
    if (cmd.empty()) {
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        cmd = std::wstring(L"\"") + exePath + L"\" --elevated";
    }

    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    DWORD creationFlags = CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;

    BOOL result = CreateProcessAsUserW(
        hPrimaryToken,
        nullptr,
        const_cast<LPWSTR>(cmd.c_str()),
        nullptr, nullptr,
        FALSE, creationFlags, nullptr, nullptr,
        &si, &pi
    );

    CloseHandle(hPrimaryToken);
    CloseHandle(hSysToken);

    if (result) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return result != FALSE;
}

bool Elevator::ElevateTo(ElevateLevel level, const std::wstring& cmdline) {
    switch (level) {
        case ElevateLevel::Admin:
            if (IsRunAsAdmin()) return true;
            return RequestAdminPrivileges();

        case ElevateLevel::System:
            if (IsRunAsSystem()) return true;
            return CreateProcessAsSystem(cmdline);

        case ElevateLevel::TrustedInstaller:
            if (IsRunAsTrustedInstaller()) return true;
            return CreateProcessAsTrustedInstaller(cmdline);
    }
    return false;
}

} // namespace diskcleaner