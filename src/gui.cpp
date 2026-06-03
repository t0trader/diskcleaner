#include "gui.h"
#include "utils.h"
#include <commctrl.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace diskcleaner {

static HMENU MakeMenuID(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

MainWindow::MainWindow() {
    scanner_ = std::make_unique<Scanner>();
    cleaner_ = std::make_unique<Cleaner>();
}

MainWindow::~MainWindow() {
    scanner_->StopScan();
    cleaner_->StopCleanup();
    if (hFont_) DeleteObject(hFont_);
    if (hFontBold_) DeleteObject(hFontBold_);
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    hInstance_ = hInstance;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = MAIN_WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) return false;

    hWnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        MAIN_WINDOW_CLASS,
        L"Disk Cleaner - Windows 11",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        nullptr, nullptr,
        hInstance, this
    );

    if (!hWnd_) return false;

    ShowWindow(hWnd_, nCmdShow);
    UpdateWindow(hWnd_);
    return true;
}

int MainWindow::Run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto pWindow = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
        return pWindow->HandleMessage(hWnd, msg, wParam, lParam);
    }

    auto pWindow = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (pWindow) {
        return pWindow->HandleMessage(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            if (!OnCreate(hWnd)) return -1;
            return 0;
        case WM_DESTROY:
            OnDestroy();
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_NOTIFY:
            OnNotify(reinterpret_cast<NMHDR*>(lParam));
            return 0;
        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
            return 0;

        // Thread-safe UI updates from scanner/cleaner threads
        case WM_SCAN_PROGRESS: {
            // wParam = category index, lParam = unused
            int catIdx = static_cast<int>(wParam);
            std::wstring status = L"Scanning: " + Scanner::GetCategoryName(static_cast<CleanupCategory>(catIdx));
            UpdateStatusText(status);
            SendMessageW(hProgressBar_, PBM_SETPOS, catIdx * 100 / static_cast<int>(CleanupCategory::Count), 0);
            return 0;
        }
        case WM_SCAN_COMPLETE: {
            UpdateButtons(false);
            UpdateListView();
            SendMessageW(hProgressBar_, PBM_SETPOS, 100, 0);
            int64_t totalSize = scanner_->GetTotalSize();
            std::wstring status = L"Scan complete - " + FormatBytes(totalSize) + L" recoverable";
            UpdateStatusText(status);
            EnableWindow(hCleanButton_, totalSize > 0);
            return 0;
        }
        case WM_CLEAN_PROGRESS: {
            int64_t bytesFreed = static_cast<int64_t>(wParam);
            int64_t totalSize = scanner_->GetTotalSize();
            if (totalSize > 0) {
                int progress = static_cast<int>(bytesFreed * 100 / totalSize);
                SendMessageW(hProgressBar_, PBM_SETPOS, progress, 0);
            }
            std::wstring sizeText = L"Freed: " + FormatBytes(bytesFreed);
            SetWindowTextW(hSizeText_, sizeText.c_str());
            return 0;
        }
        case WM_CLEAN_COMPLETE: {
            int64_t bytesFreed = static_cast<int64_t>(wParam);
            int64_t filesDeleted = static_cast<int64_t>(lParam);
            SendMessageW(hProgressBar_, PBM_SETPOS, 100, 0);
            std::wstring status = L"Done - Freed " + FormatBytes(bytesFreed) +
                L", deleted " + std::to_wstring(filesDeleted) + L" files";
            UpdateStatusText(status);
            // Rescan
            scanner_->StartScan();
            MessageBoxW(hWnd_, status.c_str(), L"Cleanup Complete", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }
    return DefWindowProcW(hWnd_, msg, wParam, lParam);
}

bool MainWindow::OnCreate(HWND hWnd) {
    hWnd_ = hWnd;
    CreateFonts();
    CreateControls();
    return true;
}

void MainWindow::OnDestroy() {
    scanner_->StopScan();
    cleaner_->StopCleanup();
}

void MainWindow::OnSize(int width, int height) {
    UpdateLayout(width, height);
}

void MainWindow::OnCommand(int id, int notifyCode) {
    switch (id) {
        case IDC_SCAN_BUTTON:   OnScanClick(); break;
        case IDC_CLEAN_BUTTON:  OnCleanClick(); break;
        case IDC_SELECT_ALL:    OnSelectAllClick(); break;
        case IDC_ELEVATE_COMBO:
            if (notifyCode == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(hElevateCombo_, CB_GETCURSEL, 0, 0));
                if (sel == 1) {
                    // SYSTEM
                    Elevator::ElevateTo(ElevateLevel::System);
                } else if (sel == 2) {
                    // TrustedInstaller
                    Elevator::ElevateTo(ElevateLevel::TrustedInstaller);
                }
            }
            break;
    }
}

void MainWindow::OnNotify(NMHDR* pNMHDR) {
    if (pNMHDR->idFrom == IDC_LISTVIEW && pNMHDR->code == LVN_ITEMCHANGED) {
        auto pNMListView = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
        if (pNMListView->uChanged & LVIF_STATE) {
            auto results = scanner_->GetResults();
            if (pNMListView->iItem >= 0 && pNMListView->iItem < static_cast<int>(results.size())) {
                bool checked = (ListView_GetCheckState(hListView_, pNMListView->iItem) != 0);
                scanner_->SetItemEnabled(results[pNMListView->iItem].category, checked);
                UpdateSizeText(scanner_->GetTotalSize());
            }
        }
    }
}

void MainWindow::OnDpiChanged(int dpi, RECT* pRect) {
    dpi_ = dpi;

    // Recreate fonts for new DPI
    if (hFont_) DeleteObject(hFont_);
    if (hFontBold_) DeleteObject(hFontBold_);
    CreateFonts();

    // Apply new fonts
    if (hFont_) {
        SendMessageW(hListView_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hScanButton_, WM_SETFONT, WPARAM(hFontBold_), TRUE);
        SendMessageW(hCleanButton_, WM_SETFONT, WPARAM(hFontBold_), TRUE);
        SendMessageW(hSelectAllCheck_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hStatusText_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hSizeText_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hElevateLabel_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hElevateCombo_, WM_SETFONT, WPARAM(hFont_), TRUE);
    }

    SetWindowPos(hWnd_, nullptr, pRect->left, pRect->top,
        pRect->right - pRect->left, pRect->bottom - pRect->top,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::CreateFonts() {
    // Create clear font for all controls
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(12, dpi_, 72);  // 12pt at current DPI
    lf.lfWeight = FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    wcscpy_s(lf.lfFaceName, L"Segoe UI Variable");  // Windows 11 best font

    hFont_ = CreateFontIndirectW(&lf);

    // Bold version for buttons
    lf.lfWeight = FW_SEMIBOLD;
    hFontBold_ = CreateFontIndirectW(&lf);
}

void MainWindow::CreateControls() {
    CreateListView();
    CreateButtons();
    CreateProgressBar();
    CreateStatusBar();
    CreateElevationCombo();

    // Apply font to all controls
    if (hFont_) {
        SendMessageW(hListView_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hScanButton_, WM_SETFONT, WPARAM(hFontBold_), TRUE);
        SendMessageW(hCleanButton_, WM_SETFONT, WPARAM(hFontBold_), TRUE);
        SendMessageW(hSelectAllCheck_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hStatusText_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hSizeText_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hElevateLabel_, WM_SETFONT, WPARAM(hFont_), TRUE);
        SendMessageW(hElevateCombo_, WM_SETFONT, WPARAM(hFont_), TRUE);
    }

    RECT rc;
    GetClientRect(hWnd_, &rc);
    UpdateLayout(rc.right, rc.bottom);
}

void MainWindow::CreateListView() {
    hListView_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_LISTVIEW),
        hInstance_, nullptr
    );

    ListView_SetExtendedListViewStyle(hListView_,
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    col.pszText = const_cast<LPWSTR>(L"Category");
    col.cx = 180;
    col.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(hListView_, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Risk");
    col.cx = 60;
    col.fmt = LVCFMT_CENTER;
    ListView_InsertColumn(hListView_, 1, &col);

    col.pszText = const_cast<LPWSTR>(L"Description");
    col.cx = 280;
    col.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(hListView_, 2, &col);

    col.pszText = const_cast<LPWSTR>(L"Size");
    col.cx = 100;
    col.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListView_, 3, &col);

    col.pszText = const_cast<LPWSTR>(L"Files");
    col.cx = 80;
    col.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListView_, 4, &col);
}

void MainWindow::CreateButtons() {
    hScanButton_ = CreateWindowExW(
        0, WC_BUTTONW, L"Scan",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_SCAN_BUTTON),
        hInstance_, nullptr
    );

    hCleanButton_ = CreateWindowExW(
        0, WC_BUTTONW, L"Clean",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_CLEAN_BUTTON),
        hInstance_, nullptr
    );
    EnableWindow(hCleanButton_, FALSE);

    hSelectAllCheck_ = CreateWindowExW(
        0, WC_BUTTONW, L"Select All",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_SELECT_ALL),
        hInstance_, nullptr
    );
    SendMessageW(hSelectAllCheck_, BM_SETCHECK, BST_CHECKED, 0);
}

void MainWindow::CreateProgressBar() {
    hProgressBar_ = CreateWindowExW(
        0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_PROGRESS),
        hInstance_, nullptr
    );
    SendMessageW(hProgressBar_, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(hProgressBar_, PBM_SETPOS, 0, 0);
}

void MainWindow::CreateStatusBar() {
    hStatusText_ = CreateWindowExW(
        0, WC_STATICW, L"Ready - Click Scan to find junk files",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_STATUS_TEXT),
        hInstance_, nullptr
    );

    hSizeText_ = CreateWindowExW(
        0, WC_STATICW, L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_SIZE_TEXT),
        hInstance_, nullptr
    );
}

void MainWindow::CreateElevationCombo() {
    // Label
    hElevateLabel_ = CreateWindowExW(
        0, WC_STATICW, L"Elevation:",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 70, 20,
        hWnd_, MakeMenuID(IDC_ELEVATE_LABEL),
        hInstance_, nullptr
    );

    // Combo box
    hElevateCombo_ = CreateWindowExW(
        0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        0, 0, 180, 100,
        hWnd_, MakeMenuID(IDC_ELEVATE_COMBO),
        hInstance_, nullptr
    );

    // Add items
    SendMessageW(hElevateCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Admin (Current)"));
    SendMessageW(hElevateCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"SYSTEM"));
    SendMessageW(hElevateCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TrustedInstaller"));

    // Set current level
    auto level = Elevator::GetCurrentLevel();
    int selIdx = 0;
    switch (level) {
        case ElevateLevel::Admin: selIdx = 0; break;
        case ElevateLevel::System: selIdx = 1; break;
        case ElevateLevel::TrustedInstaller: selIdx = 2; break;
    }
    SendMessageW(hElevateCombo_, CB_SETCURSEL, selIdx, 0);
}

void MainWindow::UpdateLayout(int width, int height) {
    const int padding = 10;
    const int buttonHeight = 35;
    const int buttonWidth = 120;
    const int statusHeight = 30;
    const int progressHeight = 25;

    int y = padding;

    // Top row: select all + elevation combo
    MoveWindow(hSelectAllCheck_, padding, y, 100, 20, TRUE);
    MoveWindow(hElevateLabel_, width - padding - 250, y, 70, 20, TRUE);
    MoveWindow(hElevateCombo_, width - padding - 180, y, 180, 25, TRUE);
    y += 30;

    // List view
    int listHeight = height - y - statusHeight - progressHeight - buttonHeight - padding * 4;
    if (listHeight < 100) listHeight = 100;
    MoveWindow(hListView_, padding, y, width - padding * 2, listHeight, TRUE);
    y += listHeight + padding;

    // Progress bar
    MoveWindow(hProgressBar_, padding, y, width - padding * 2, progressHeight, TRUE);
    y += progressHeight + padding;

    // Status row
    MoveWindow(hStatusText_, padding, y, width - padding * 2 - 200, statusHeight, TRUE);
    MoveWindow(hSizeText_, width - padding - 200, y, 200, statusHeight, TRUE);
    y += statusHeight + padding;

    // Buttons
    int buttonX = width - padding - buttonWidth * 2 - padding;
    MoveWindow(hScanButton_, buttonX, y, buttonWidth, buttonHeight, TRUE);
    MoveWindow(hCleanButton_, buttonX + buttonWidth + padding, y, buttonWidth, buttonHeight, TRUE);
}

void MainWindow::UpdateListView() {
    ListView_DeleteAllItems(hListView_);
    auto results = scanner_->GetResults();
    for (int i = 0; i < static_cast<int>(results.size()); i++) {
        SetListViewItem(i, results[i]);
    }
    UpdateSizeText(scanner_->GetTotalSize());
}

void MainWindow::UpdateButtons(bool scanning) {
    EnableWindow(hScanButton_, !scanning);
    SetWindowTextW(hScanButton_, scanning ? L"Scanning..." : L"Scan");

    if (!scanning) {
        auto results = scanner_->GetResults();
        bool hasItems = false;
        for (const auto& item : results) {
            if (item.enabled && item.size > 0) {
                hasItems = true;
                break;
            }
        }
        EnableWindow(hCleanButton_, hasItems);
    } else {
        EnableWindow(hCleanButton_, FALSE);
    }
}

void MainWindow::UpdateStatusText(const std::wstring& text) {
    SetWindowTextW(hStatusText_, text.c_str());
}

void MainWindow::UpdateSizeText(int64_t totalSize) {
    std::wstring text = L"Recoverable: " + FormatBytes(totalSize);
    SetWindowTextW(hSizeText_, text.c_str());
}

void MainWindow::OnScanClick() {
    if (scanner_->IsScanning()) {
        scanner_->StopScan();
        UpdateButtons(false);
        UpdateStatusText(L"Scan cancelled");
        return;
    }
    UpdateButtons(true);
    UpdateStatusText(L"Scanning...");
    SendMessageW(hProgressBar_, PBM_SETPOS, 0, 0);

    // Set callbacks that post messages to UI thread
    scanner_->SetProgressCallback([this](CleanupCategory cat, const std::wstring&) {
        PostMessageW(hWnd_, WM_SCAN_PROGRESS, static_cast<WPARAM>(cat), 0);
    });
    scanner_->SetCompleteCallback([this](const std::vector<CleanupItem>&) {
        PostMessageW(hWnd_, WM_SCAN_COMPLETE, 0, 0);
    });

    scanner_->StartScan();
}

void MainWindow::OnCleanClick() {
    if (cleaner_->IsCleaning()) {
        cleaner_->StopCleanup();
        UpdateButtons(false);
        UpdateStatusText(L"Cleanup cancelled");
        return;
    }

    auto results = scanner_->GetResults();
    int64_t totalSize = scanner_->GetTotalSize();
    std::wstring msg = L"Clean " + FormatBytes(totalSize) + L" of junk files?";
    int ret = MessageBoxW(hWnd_, msg.c_str(), L"Confirm Cleanup",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (ret != IDYES) return;

    UpdateButtons(true);
    SetWindowTextW(hCleanButton_, L"Cleaning...");
    UpdateStatusText(L"Cleaning...");
    SendMessageW(hProgressBar_, PBM_SETPOS, 0, 0);

    // Set callbacks that post messages to UI thread
    cleaner_->SetProgressCallback([this](CleanupCategory, const std::wstring&, int64_t bytesFreed) {
        PostMessageW(hWnd_, WM_CLEAN_PROGRESS, static_cast<WPARAM>(bytesFreed), 0);
    });
    cleaner_->SetCompleteCallback([this](int64_t bytesFreed, int64_t filesDeleted) {
        PostMessageW(hWnd_, WM_CLEAN_COMPLETE, static_cast<WPARAM>(bytesFreed), static_cast<LPARAM>(filesDeleted));
    });

    cleaner_->StartCleanup(results);
}

void MainWindow::OnSelectAllClick() {
    allSelected_ = !allSelected_;
    SendMessageW(hSelectAllCheck_, BM_SETCHECK,
        allSelected_ ? BST_CHECKED : BST_UNCHECKED, 0);

    auto results = scanner_->GetResults();
    for (size_t i = 0; i < results.size(); i++) {
        ListView_SetCheckState(hListView_, i, allSelected_);
        scanner_->SetItemEnabled(results[i].category, allSelected_);
    }
    UpdateSizeText(scanner_->GetTotalSize());
}

void MainWindow::SetListViewItem(int index, const CleanupItem& item) {
    LVITEMW lvi = {};
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    lvi.iItem = index;
    lvi.iSubItem = 0;
    lvi.pszText = const_cast<LPWSTR>(item.name.c_str());
    lvi.lParam = static_cast<LPARAM>(index);

    if (index == ListView_GetItemCount(hListView_)) {
        ListView_InsertItem(hListView_, &lvi);
    } else {
        ListView_SetItem(hListView_, &lvi);
    }

    // Risk level (column 1)
    std::wstring riskStr = Scanner::GetRiskLevelName(item.risk);
    ListView_SetItemText(hListView_, index, 1, const_cast<LPWSTR>(riskStr.c_str()));

    // Description (column 2)
    ListView_SetItemText(hListView_, index, 2, const_cast<LPWSTR>(item.description.c_str()));

    // Size (column 3)
    std::wstring sizeStr = FormatBytes(item.size);
    ListView_SetItemText(hListView_, index, 3, const_cast<LPWSTR>(sizeStr.c_str()));

    // Files (column 4)
    std::wstring countStr = std::to_wstring(item.fileCount);
    ListView_SetItemText(hListView_, index, 4, const_cast<LPWSTR>(countStr.c_str()));

    // Checkbox - default enabled based on risk
    ListView_SetCheckState(hListView_, index, item.enabled);
}

} // namespace diskcleaner