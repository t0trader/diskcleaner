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

// 列表列宽（96 DPI 基准），实际使用时按当前 DPI 缩放
static const int kColumnWidths[] = {180, 60, 280, 100, 80};

// 丢弃已入队但未处理的进度消息，避免取消后旧进度覆盖状态文本
static void DrainProgressMessages(HWND hwnd, UINT msgId) {
    MSG msg;
    while (PeekMessageW(&msg, hwnd, msgId, msgId, PM_REMOVE)) {}
}

// 不产生文件大小但确实执行系统操作的类别（大小可能为 0 仍允许清理）
static bool IsCommandCleanup(CleanupCategory category) {
    switch (category) {
        case CleanupCategory::HibernationFile:
        case CleanupCategory::EventLogsWEvtUtil:
        case CleanupCategory::FlushDNS:
        case CleanupCategory::CrashDumpSettings:
            return true;
        default:
            return false;
    }
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

    // 按系统 DPI 缩放初始窗口尺寸，避免高 DPI 下界面过小
    UINT sysDpi = GetDpiForSystem();
    if (sysDpi == 0) sysDpi = 96;

    hWnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        MAIN_WINDOW_CLASS,
        L"Disk Cleaner - Windows 11",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        MulDiv(800, static_cast<int>(sysDpi), 96),
        MulDiv(600, static_cast<int>(sysDpi), 96),
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
            int total = static_cast<int>(CleanupCategory::Count);
            std::wstring status = L"Scanning: " + Scanner::GetCategoryName(static_cast<CleanupCategory>(catIdx));
            UpdateStatusText(status);
            int progress = total > 0 ? catIdx * 100 / total : 0;
            if (progress > 100) progress = 100;
            if (progress < 0) progress = 0;
            SendMessageW(hProgressBar_, PBM_SETPOS, progress, 0);
            return 0;
        }
        case WM_SCAN_COMPLETE: {
            UpdateActionButtons();
            UpdateListView();
            SendMessageW(hProgressBar_, PBM_SETPOS, 100, 0);
            int64_t totalSize = scanner_->GetTotalSize();
            std::wstring status = L"Scan complete - " + FormatBytes(totalSize) + L" recoverable";
            UpdateStatusText(status);
            return 0;
        }
        case WM_CLEAN_PROGRESS: {
            int64_t bytesFreed = static_cast<int64_t>(wParam);
            // 使用清理开始时缓存的总量，避免每个进度消息都遍历/加锁全部扫描结果
            if (cleanTotalSize_ > 0) {
                int progress = static_cast<int>(bytesFreed * 100 / cleanTotalSize_);
                if (progress > 100) progress = 100;
                if (progress < 0) progress = 0;
                SendMessageW(hProgressBar_, PBM_SETPOS, progress, 0);
            }
            std::wstring sizeText = L"Freed: " + FormatBytes(bytesFreed);
            SetWindowTextW(hSizeText_, sizeText.c_str());
            return 0;
        }
        case WM_CLEAN_COMPLETE: {
            int64_t bytesFreed = static_cast<int64_t>(wParam);
            int64_t filesDeleted = static_cast<int64_t>(lParam);
            int64_t filesFailed = cleaner_->GetFilesFailed();
            SendMessageW(hProgressBar_, PBM_SETPOS, 100, 0);
            std::wstring status = L"Done - Freed " + FormatBytes(bytesFreed) +
                L", deleted " + std::to_wstring(filesDeleted) + L" files";
            if (filesFailed > 0) {
                status += L", skipped " + std::to_wstring(filesFailed) + L" (in use)";
            }
            if (restorePointId_ != 0) {
                status += L" (restore point created)";
            }
            UpdateStatusText(status);
            MessageBoxW(hWnd_, status.c_str(), L"Cleanup Complete", MB_OK | MB_ICONINFORMATION);
            // 重新扫描（复用 OnScanClick 以正确更新按钮状态）
            OnScanClick();
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool MainWindow::OnCreate(HWND hWnd) {
    hWnd_ = hWnd;
    UINT dpi = GetDpiForWindow(hWnd);
    dpi_ = dpi != 0 ? static_cast<int>(dpi) : 96;
    CreateFonts();
    CreateControls();
    return true;
}

void MainWindow::OnDestroy() {
    scanner_->StopScan();
    cleaner_->StopCleanup();
}

void MainWindow::OnSize(int width, int height) {
    if (width <= 0 || height <= 0) return;  // 最小化时避免出现负尺寸
    UpdateLayout(width, height);
}

void MainWindow::OnCommand(int id, int notifyCode) {
    switch (id) {
        case IDC_SCAN_BUTTON:   OnScanClick(); break;
        case IDC_CLEAN_BUTTON:  OnCleanClick(); break;
        case IDC_SELECT_ALL:    OnSelectAllClick(); break;
        case IDC_RESTORE_BUTTON: OnRestoreClick(); break;
        case IDC_ELEVATE_COMBO:
            if (notifyCode == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(hElevateCombo_, CB_GETCURSEL, 0, 0));
                if (sel != 1 && sel != 2) break;

                ElevateLevel level = (sel == 1) ? ElevateLevel::System
                                                : ElevateLevel::TrustedInstaller;
                bool already = (sel == 1) ? Elevator::IsRunAsSystem()
                                          : Elevator::IsRunAsTrustedInstaller();

                if (!already) {
                    EnableWindow(hElevateCombo_, FALSE);
                    UpdateStatusText(L"Requesting elevated privileges...");
                    std::wstring err;
                    if (Elevator::ElevateTo(level, &err)) {
                        // 新实例已启动，退出当前进程避免出现两个窗口
                        ExitProcess(0);
                    }
                    EnableWindow(hElevateCombo_, TRUE);
                    std::wstring msg = L"Failed to elevate privileges.";
                    if (!err.empty()) msg += L"\n\n" + err;
                    MessageBoxW(hWnd_, msg.c_str(), L"Elevation Failed",
                                MB_OK | MB_ICONERROR);
                }

                // 恢复到当前实际权限级别
                std::wstring current = Elevator::GetCurrentLevel();
                int selIdx = 0;
                if (current == L"SYSTEM") selIdx = 1;
                else if (current == L"TrustedInstaller") selIdx = 2;
                SendMessageW(hElevateCombo_, CB_SETCURSEL, selIdx, 0);
                UpdateStatusText(L"Ready");
            }
            break;
    }
}

void MainWindow::OnNotify(NMHDR* pNMHDR) {
    if (pNMHDR->idFrom != IDC_LISTVIEW || pNMHDR->code != LVN_ITEMCHANGED) return;
    // 列表重建/批量勾选期间由代码主动同步，忽略控件回传的通知，避免覆盖扫描结果
    if (updatingList_) return;

    auto pNMListView = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
    if ((pNMListView->uChanged & LVIF_STATE) == 0) return;
    // 只关心勾选状态变化，忽略选中/焦点变化
    if (((pNMListView->uOldState ^ pNMListView->uNewState) & LVIS_STATEIMAGEMASK) == 0) {
        return;
    }

    const int index = pNMListView->iItem;
    if (index < 0 || index >= static_cast<int>(CleanupCategory::Count)) return;

    bool checked = ListView_GetCheckState(hListView_, index) != 0;
    scanner_->SetItemEnabled(static_cast<CleanupCategory>(index), checked);
    UpdateSizeText(scanner_->GetTotalSize());
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
        SendMessageW(hRestoreButton_, WM_SETFONT, WPARAM(hFontBold_), TRUE);
    }

    // 列宽随 DPI 重新缩放
    for (int i = 0; i < static_cast<int>(_countof(kColumnWidths)); i++) {
        ListView_SetColumnWidth(hListView_, i, Scale(kColumnWidths[i]));
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

    // 按钮字体：14pt 加粗，更醒目、更易读
    lf.lfHeight = -MulDiv(14, dpi_, 72);
    lf.lfWeight = FW_BOLD;
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
        SendMessageW(hRestoreButton_, WM_SETFONT, WPARAM(hFontBold_), TRUE);
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
    col.cx = Scale(kColumnWidths[0]);
    col.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(hListView_, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Risk");
    col.cx = Scale(kColumnWidths[1]);
    col.fmt = LVCFMT_CENTER;
    ListView_InsertColumn(hListView_, 1, &col);

    col.pszText = const_cast<LPWSTR>(L"Description");
    col.cx = Scale(kColumnWidths[2]);
    col.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(hListView_, 2, &col);

    col.pszText = const_cast<LPWSTR>(L"Size");
    col.cx = Scale(kColumnWidths[3]);
    col.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListView_, 3, &col);

    col.pszText = const_cast<LPWSTR>(L"Files");
    col.cx = Scale(kColumnWidths[4]);
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

    // 系统还原按钮（一键回滚保护）
    hRestoreButton_ = CreateWindowExW(
        0, WC_BUTTONW, L"System Restore",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd_, MakeMenuID(IDC_RESTORE_BUTTON),
        hInstance_, nullptr
    );
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
    std::wstring level = Elevator::GetCurrentLevel();
    int selIdx = 0;
    if (level == L"SYSTEM") selIdx = 1;
    else if (level == L"TrustedInstaller") selIdx = 2;
    SendMessageW(hElevateCombo_, CB_SETCURSEL, selIdx, 0);
}

void MainWindow::UpdateLayout(int width, int height) {
    const int padding = Scale(10);
    const int buttonHeight = Scale(40);
    const int buttonWidth = Scale(140);
    const int restoreButtonWidth = Scale(180);  // System Restore 文字较长，单独加宽
    const int statusHeight = Scale(30);
    const int progressHeight = Scale(25);
    const int rowHeight = Scale(20);
    const int comboWidth = Scale(180);
    const int labelWidth = Scale(70);
    const int contentWidth = width - padding * 2 > Scale(50)
        ? width - padding * 2 : Scale(50);

    int y = padding;

    // Top row: select all + elevation combo
    MoveWindow(hSelectAllCheck_, padding, y, Scale(100), rowHeight, TRUE);
    MoveWindow(hElevateLabel_, width - padding - Scale(250), y, labelWidth, rowHeight, TRUE);
    MoveWindow(hElevateCombo_, width - padding - comboWidth, y, comboWidth, Scale(25), TRUE);
    y += Scale(30);

    // List view
    int listHeight = height - y - statusHeight - progressHeight - buttonHeight - padding * 4;
    if (listHeight < Scale(100)) listHeight = Scale(100);
    MoveWindow(hListView_, padding, y, contentWidth, listHeight, TRUE);
    y += listHeight + padding;

    // Progress bar
    MoveWindow(hProgressBar_, padding, y, contentWidth, progressHeight, TRUE);
    y += progressHeight + padding;

    // Status row
    int statusWidth = contentWidth - Scale(200);
    if (statusWidth < Scale(100)) statusWidth = Scale(100);
    MoveWindow(hStatusText_, padding, y, statusWidth, statusHeight, TRUE);
    MoveWindow(hSizeText_, width - padding - Scale(200), y, Scale(200), statusHeight, TRUE);
    y += statusHeight + padding;

    // Buttons: System Restore 在左（单独加宽），Scan/Clean 在右
    MoveWindow(hRestoreButton_, padding, y, restoreButtonWidth, buttonHeight, TRUE);
    int buttonX = width - padding - buttonWidth * 2 - padding;
    if (buttonX < padding) buttonX = padding;
    MoveWindow(hScanButton_, buttonX, y, buttonWidth, buttonHeight, TRUE);
    MoveWindow(hCleanButton_, buttonX + buttonWidth + padding, y, buttonWidth, buttonHeight, TRUE);
}

void MainWindow::UpdateListView() {
    // 重建列表期间抑制 LVN_ITEMCHANGED，避免控件回传的中间状态覆盖扫描结果
    updatingList_ = true;
    ListView_DeleteAllItems(hListView_);
    auto results = scanner_->GetResults();
    for (int i = 0; i < static_cast<int>(results.size()); i++) {
        SetListViewItem(i, results[i]);
    }
    updatingList_ = false;
    UpdateSizeText(scanner_->GetTotalSize());
}

void MainWindow::UpdateActionButtons() {
    const bool scanning = scanner_->IsScanning();
    const bool cleaning = cleaner_->IsCleaning();

    SetWindowTextW(hScanButton_, scanning ? L"Cancel" : L"Scan");
    SetWindowTextW(hCleanButton_, cleaning ? L"Cancel" : L"Clean");

    // 扫描/清理过程中按钮变为“取消”并保持可用；两者互斥
    EnableWindow(hScanButton_, !cleaning);
    EnableWindow(hRestoreButton_, !scanning && !cleaning);
    EnableWindow(hElevateCombo_, !scanning && !cleaning);

    bool hasItems = cleaning;  // 清理过程中保持“取消”可用
    if (!hasItems) {
        auto results = scanner_->GetResults();
        for (const auto& item : results) {
            if (!item.enabled || !item.scanned) continue;
            if (item.size > 0 || IsCommandCleanup(item.category)) {
                hasItems = true;
                break;
            }
        }
    }
    EnableWindow(hCleanButton_, hasItems && !scanning);
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
        DrainProgressMessages(hWnd_, WM_SCAN_PROGRESS);
        UpdateActionButtons();
        UpdateStatusText(L"Scan cancelled");
        return;
    }
    if (cleaner_->IsCleaning()) return;  // 清理期间不允许启动扫描

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
    UpdateActionButtons();
}

void MainWindow::OnCleanClick() {
    if (cleaner_->IsCleaning()) {
        cleaner_->StopCleanup();
        DrainProgressMessages(hWnd_, WM_CLEAN_PROGRESS);
        UpdateActionButtons();
        UpdateStatusText(L"Cleanup cancelled");
        return;
    }
    if (scanner_->IsScanning()) return;  // 扫描期间不允许清理

    auto results = scanner_->GetResults();
    int64_t totalSize = scanner_->GetTotalSize();
    cleanTotalSize_ = totalSize;

    // Count cleanable items (ignore empty file-based categories like Cleaner does)
    int cleanableCount = 0;
    std::wstring highRiskList;
    int highRiskCount = 0;
    for (const auto& item : results) {
        if (!item.enabled || !item.scanned) continue;
        if (item.size == 0 && !IsCommandCleanup(item.category)) continue;
        cleanableCount++;
        if (item.risk == RiskLevel::High ||
            item.category == CleanupCategory::CrashDumpSettings) {
            highRiskList += L"\n  - " + item.name;
            highRiskCount++;
        }
    }

    std::wstring msg;
    if (totalSize > 0) {
        msg = L"Clean " + FormatBytes(totalSize) + L" from " + std::to_wstring(cleanableCount) + L" categories?";
    } else {
        msg = L"Clean " + std::to_wstring(cleanableCount) + L" categories (some have 0 estimated size)?";
    }
    if (highRiskCount > 0) {
        msg += L"\n\nHigh-risk / system-setting categories selected:" + highRiskList;
    }
    int ret = MessageBoxW(hWnd_, msg.c_str(), L"Confirm Cleanup",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (ret != IDYES) return;

    // 清理前自动创建系统还原点，作为故障回滚保护
    EnableWindow(hScanButton_, FALSE);
    EnableWindow(hCleanButton_, FALSE);
    EnableWindow(hRestoreButton_, FALSE);
    SetWindowTextW(hScanButton_, L"Scan");
    SetWindowTextW(hCleanButton_, L"Preparing...");
    restorePointId_ = 0;
    UpdateStatusText(L"Creating system restore point...");
    SendMessageW(hProgressBar_, PBM_SETPOS, 0, 0);
    UpdateWindow(hWnd_);  // 还原点创建可能耗时，先刷新界面

    restorePointId_ = CreateSystemRestorePoint(L"DiskCleaner before cleanup");
    if (restorePointId_ == 0) {
        int retry = MessageBoxW(hWnd_,
            L"Failed to create a system restore point (System Restore may be disabled).\n"
            L"Continue cleanup anyway?",
            L"Warning", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (retry != IDYES) {
            UpdateActionButtons();
            UpdateStatusText(L"Cleanup cancelled");
            return;
        }
    }

    UpdateStatusText(L"Cleaning...");

    // Set callbacks that post messages to UI thread
    cleaner_->SetProgressCallback([this](CleanupCategory, const std::wstring&, int64_t bytesFreed) {
        PostMessageW(hWnd_, WM_CLEAN_PROGRESS, static_cast<WPARAM>(bytesFreed), 0);
    });
    cleaner_->SetCompleteCallback([this](int64_t bytesFreed, int64_t filesDeleted) {
        PostMessageW(hWnd_, WM_CLEAN_COMPLETE, static_cast<WPARAM>(bytesFreed), static_cast<LPARAM>(filesDeleted));
    });

    cleaner_->StartCleanup(results);
    UpdateActionButtons();
}

void MainWindow::OnSelectAllClick() {
    // BS_AUTOCHECKBOX 已切换控件状态，以控件实际状态为准（可能被用户单独取消过某些项）
    const bool checked = SendMessageW(hSelectAllCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;

    updatingList_ = true;
    for (int i = 0; i < static_cast<int>(CleanupCategory::Count); i++) {
        ListView_SetCheckState(hListView_, i, checked);
        scanner_->SetItemEnabled(static_cast<CleanupCategory>(i), checked);
    }
    updatingList_ = false;
    UpdateSizeText(scanner_->GetTotalSize());
}

void MainWindow::OnRestoreClick() {
    // 打开系统还原界面，用户可选择清理前创建的还原点回滚
    if (!OpenSystemRestore()) {
        MessageBoxW(hWnd_,
            L"Failed to open System Restore.\n"
            L"Please search for 'Create a restore point' in the Start menu.",
            L"Error", MB_OK | MB_ICONERROR);
    }
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