#include <windows.h>
#include <string>
#include "gui.h"
#include "elevator.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    // Parse command line for --elevated flag
    std::wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
    bool isElevated = (cmdLine.find(L"--elevated") != std::wstring::npos);

    // Get current privilege level
    auto level = diskcleaner::Elevator::GetCurrentLevel();

    // If not elevated and not admin, request admin
    if (!isElevated && !diskcleaner::Elevator::IsRunAsAdmin()) {
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