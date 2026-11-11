#include <windows.h>
#include <string>
#include "gui.h"
#include "elevator.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // 已提权实例直接运行。TrustedInstaller 令牌不一定包含 Administrators 组，
    // 必须单独判断，否则会反复触发提权。
    const bool elevated = diskcleaner::Elevator::IsRunAsAdmin() ||
                          diskcleaner::Elevator::IsRunAsSystem() ||
                          diskcleaner::Elevator::IsRunAsTrustedInstaller();

    if (!elevated) {
        if (!diskcleaner::Elevator::RequestAdminPrivileges()) {
            MessageBoxW(nullptr,
                L"This program requires administrator privileges.",
                L"Insufficient Privileges", MB_OK | MB_ICONWARNING);
        }
        return 0;
    }

    // Create and run main window
    diskcleaner::MainWindow window;
    if (!window.Create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    return window.Run();
}
