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
    // Check current privilege level
    static bool IsRunAsAdmin();
    static bool IsRunAsSystem();
    static bool IsRunAsTrustedInstaller();

    // Request admin privileges (UAC prompt)
    static bool RequestAdminPrivileges();

    // Elevate to SYSTEM or TrustedInstaller
    // This will create a new elevated process and exit current one
    static bool ElevateTo(ElevateLevel level, const std::wstring& cmdline = L"");

    // Get current process token elevation info
    static ElevateLevel GetCurrentLevel();

    // Enable a specific privilege in current token
    static bool EnablePrivilege(const std::wstring& privilegeName);

private:
    // Create process as TrustedInstaller using service method
    static bool CreateProcessAsTrustedInstaller(const std::wstring& cmdline);

    // Create process as SYSTEM using token duplication
    static bool CreateProcessAsSystem(const std::wstring& cmdline);

    // Find TrustedInstaller service PID
    static DWORD FindTrustedInstallerPid();

    // Duplicate token from another process
    static HANDLE DuplicateTokenFromPid(DWORD pid);

    // Create temporary service for elevation
    static bool CreateTempService(const std::wstring& serviceName,
                                  const std::wstring& binPath,
                                  const std::wstring& account);

    // Delete temporary service
    static bool DeleteTempService(const std::wstring& serviceName);
};

} // namespace diskcleaner