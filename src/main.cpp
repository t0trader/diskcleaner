#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#include <string>
#include "gui.h"
#include "elevator.h"

namespace {

struct LaunchPlacement {
    RECT rect{};
    bool hasRect = false;
    bool maximized = false;
};

// 解析提权实例命令行中的 --pos left top width height 与 --max。
// 坐标可为负数（窗口位于主显示器左侧/上方）。
LaunchPlacement ParseLaunchPlacement() {
    LaunchPlacement placement;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return placement;

    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--max") == 0) {
            placement.maximized = true;
            continue;
        }
        if (wcscmp(argv[i], L"--pos") != 0 || i + 4 >= argc) {
            continue;
        }

        LONG left = wcstol(argv[i + 1], nullptr, 10);
        LONG top = wcstol(argv[i + 2], nullptr, 10);
        LONG width = wcstol(argv[i + 3], nullptr, 10);
        LONG height = wcstol(argv[i + 4], nullptr, 10);
        if (width > 0 && height > 0) {
            placement.rect = { left, top, left + width, top + height };
            placement.hasRect = true;
        }
        i += 4;
    }

    LocalFree(argv);
    return placement;
}

} // namespace

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

    // 提权切换时保留原窗口位置，避免新窗口跳到屏幕其他位置
    const LaunchPlacement placement = ParseLaunchPlacement();

    // Create and run main window
    diskcleaner::MainWindow window;
    if (!window.Create(hInstance, nCmdShow,
                       placement.hasRect ? &placement.rect : nullptr,
                       placement.maximized)) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    return window.Run();
}
