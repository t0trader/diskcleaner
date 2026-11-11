#pragma once

#include <windows.h>
#include <string>

namespace diskcleaner {

// Privilege elevation levels
enum class ElevateLevel {
    Admin,              // Standard admin (current process)
    System,             // SYSTEM privilege
    TrustedInstaller    // TrustedInstaller privilege
};

class Elevator {
public:
    // Check current privilege level (direct token inspection)
    static std::wstring GetCurrentLevel();

    // Check specific privilege levels
    static bool IsRunAsAdmin();
    static bool IsRunAsSystem();
    static bool IsRunAsTrustedInstaller();

    // Request admin privileges (UAC prompt) — exits current process
    static bool RequestAdminPrivileges();

    // Elevate to SYSTEM or TrustedInstaller.
    // 启动一个新的提权实例（传入 --elevated），成功返回 true；
    // 调用方应在成功后退出当前进程，失败时通过 err 获取原因。
    static bool ElevateTo(ElevateLevel level, std::wstring* err = nullptr);

    // Enable a specific privilege in current token
    static bool EnablePrivilege(const std::wstring& privilegeName);

private:
    // Enable SeDebugPrivilege and get session ID
    static bool EnableSeDebugPrivilege(DWORD& sessionId, std::wstring* err = nullptr);

    // Find winlogon.exe PID in given session using WTS
    static DWORD LookupWinlogon(DWORD sessionId, std::wstring* err = nullptr);

    // Enable ONE privilege on an impersonation token
    static void EnableOnePrivilege(HANDLE hToken, const wchar_t* name);

    // Enable ALL privileges on an impersonation token (Privexec style)
    static void EnableAllPrivileges(HANDLE hToken);

    // Start TrustedInstaller service with proper wait logic, return PID
    static bool StartTIService(DWORD& pid, std::wstring* err = nullptr);

    // Launch process with primary token on WinSta0\Default desktop
    static bool LaunchProcess(const std::wstring& exePath, const std::wstring& args,
                              HANDLE hPrimaryToken, std::wstring* err = nullptr);

    // SYSTEM elevation: Privexec execute_with_system flow
    static bool RunAsSystem(const std::wstring& exePath, std::wstring* err = nullptr);

    // TrustedInstaller elevation: Privexec execute_with_ti flow
    static bool RunAsTrustedInstaller(const std::wstring& exePath, std::wstring* err = nullptr);
};

} // namespace diskcleaner
