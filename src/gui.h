#pragma once

#include <windows.h>
#include <commctrl.h>
#include <memory>
#include "scanner.h"
#include "cleaner.h"
#include "elevator.h"

namespace diskcleaner {

constexpr wchar_t MAIN_WINDOW_CLASS[] = L"DiskCleanerMainWindow";

constexpr int IDC_SCAN_BUTTON   = 1001;
constexpr int IDC_CLEAN_BUTTON  = 1002;
constexpr int IDC_SELECT_ALL    = 1003;
constexpr int IDC_LISTVIEW      = 1004;
constexpr int IDC_PROGRESS      = 1005;
constexpr int IDC_STATUS_TEXT   = 1006;
constexpr int IDC_SIZE_TEXT     = 1007;
constexpr int IDC_ELEVATE_COMBO = 1008;
constexpr int IDC_ELEVATE_LABEL = 1009;
constexpr int IDC_RESTORE_BUTTON = 1010;

// Custom messages for thread-safe UI updates
constexpr UINT WM_SCAN_PROGRESS = WM_USER + 10;
constexpr UINT WM_SCAN_COMPLETE = WM_USER + 11;
constexpr UINT WM_CLEAN_PROGRESS = WM_USER + 12;
constexpr UINT WM_CLEAN_COMPLETE = WM_USER + 13;

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow,
                const RECT* initialRect = nullptr, bool maximized = false);
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool OnCreate(HWND hWnd);
    void OnDestroy();
    void OnSize(int width, int height);
    void OnCommand(int id, int notifyCode);
    void OnNotify(NMHDR* pNMHDR);
    void OnDpiChanged(int dpi, RECT* pRect);

    void CreateControls();
    void CreateListView();
    void CreateButtons();
    void CreateProgressBar();
    void CreateStatusBar();
    void CreateElevationCombo();

    void UpdateLayout(int width, int height);
    void UpdateListView();
    void UpdateActionButtons();
    void UpdateStatusText(const std::wstring& text);
    void UpdateSizeText(int64_t totalSize);

    int Scale(int value) const { return MulDiv(value, dpi_, 96); }

    void OnScanClick();
    void OnCleanClick();
    void OnSelectAllClick();
    void OnRestoreClick();

    void SetListViewItem(int index, const CleanupItem& item);

    void CreateFonts();

    HWND hWnd_ = nullptr;
    HWND hListView_ = nullptr;
    HWND hScanButton_ = nullptr;
    HWND hCleanButton_ = nullptr;
    HWND hSelectAllCheck_ = nullptr;
    HWND hProgressBar_ = nullptr;
    HWND hStatusText_ = nullptr;
    HWND hSizeText_ = nullptr;
    HWND hElevateCombo_ = nullptr;
    HWND hElevateLabel_ = nullptr;
    HWND hRestoreButton_ = nullptr;

    HINSTANCE hInstance_ = nullptr;
    int dpi_ = 96;

    HFONT hFont_ = nullptr;
    HFONT hFontBold_ = nullptr;

    std::unique_ptr<Scanner> scanner_;
    std::unique_ptr<Cleaner> cleaner_;

    bool updatingList_ = false;
    int64_t restorePointId_ = 0;
    int64_t cleanTotalSize_ = 0;
};

} // namespace diskcleaner