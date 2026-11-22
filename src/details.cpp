#include "details.h"
#include "category_paths.h"
#include "chromium_cache.h"
#include "safety.h"
#include "smart.h"
#include "utils.h"

#include <commctrl.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace diskcleaner {

namespace {

constexpr wchar_t DETAILS_WINDOW_CLASS[] = L"DiskCleanerDetailsWindow";

constexpr int IDC_DETAILS_LIST = 2001;
constexpr int IDC_DETAILS_DELETE = 2002;
constexpr int IDC_DETAILS_CLOSE = 2003;
constexpr int IDC_DETAILS_HEADER = 2004;
constexpr UINT WM_DETAILS_LOADED = WM_APP + 1;
constexpr UINT WM_DETAILS_CLOSED = WM_APP + 2;  // 线程消息，避免 PostQuitMessage 终止主循环

struct DetailsState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    HWND header = nullptr;
    HWND list = nullptr;
    HWND deleteButton = nullptr;
    HWND closeButton = nullptr;
    CleanupCategory category = CleanupCategory::Count;
    std::wstring categoryName;
    int dpi = 96;
    bool siteMode = false;
    std::vector<SiteCacheEntry> sites;
    std::vector<TopEntry> entries;
};

int ScaleOf(const DetailsState* state, int value) {
    return MulDiv(value, state->dpi, 96);
}

void SetHeaderText(DetailsState* state, const std::wstring& extra) {
    if (!state->header) return;
    int64_t total = 0;
    int64_t count = 0;
    if (state->siteMode) {
        for (const auto& s : state->sites) {
            total += s.size;
            if (s.size > 0) ++count;
        }
    } else {
        for (const auto& e : state->entries) {
            total += e.size;
            if (e.size > 0) ++count;
        }
    }
    std::wstring text = state->categoryName + L" - " + FormatBytes(total) +
                        L" in " + std::to_wstring(count) + L" entries";
    if (!extra.empty()) text += L"  |  " + extra;
    SetWindowTextW(state->header, text.c_str());
}

void FillList(DetailsState* state) {
    if (!state->list) return;
    SendMessageW(state->list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(state->list);

    const size_t count = state->siteMode ? state->sites.size() : state->entries.size();
    for (size_t i = 0; i < count; ++i) {
        std::wstring name, age, size;
        if (state->siteMode) {
            name = state->sites[i].origin;
            age = state->sites[i].lastUsedText;
            size = FormatBytes(state->sites[i].size);
        } else {
            name = state->entries[i].name;
            if (state->entries[i].isDirectory) name += L"\\";
            age = state->entries[i].lastUsedText;
            size = FormatBytes(state->entries[i].size);
        }

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(name.c_str());
        ListView_InsertItem(state->list, &item);
        ListView_SetItemText(state->list, static_cast<int>(i), 1, const_cast<LPWSTR>(age.c_str()));
        ListView_SetItemText(state->list, static_cast<int>(i), 2, const_cast<LPWSTR>(size.c_str()));
        if (state->siteMode) {
            ListView_SetCheckState(state->list, static_cast<int>(i), FALSE);
        }
    }

    SendMessageW(state->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state->list, nullptr, TRUE);
    SetHeaderText(state, L"");
}

void LoadData(DetailsState* state) {
    state->sites.clear();
    state->entries.clear();

    if (state->siteMode) {
        for (const auto& path : GetCategoryPaths(state->category)) {
            if (!DirectoryExists(path)) continue;
            auto sites = AnalyzeCacheStorage(path, nullptr);
            for (auto& site : sites) {
                if (site.size > 0) state->sites.push_back(std::move(site));
            }
        }
        std::sort(state->sites.begin(), state->sites.end(),
            [](const SiteCacheEntry& a, const SiteCacheEntry& b) { return a.size > b.size; });
    } else {
        for (const auto& path : GetCategoryPaths(state->category)) {
            if (!DirectoryExists(path)) continue;
            auto top = GetTopEntries(path, 10, nullptr);
            for (auto& entry : top) {
                if (entry.size > 0) state->entries.push_back(std::move(entry));
            }
        }
        std::sort(state->entries.begin(), state->entries.end(),
            [](const TopEntry& a, const TopEntry& b) { return a.size > b.size; });
        if (state->entries.size() > 15) state->entries.resize(15);
    }
}

void DeleteSelected(DetailsState* state) {
    if (!state->siteMode) return;

    std::vector<size_t> selected;
    int64_t total = 0;
    for (size_t i = 0; i < state->sites.size(); ++i) {
        if (ListView_GetCheckState(state->list, static_cast<int>(i))) {
            selected.push_back(i);
            total += state->sites[i].size;
        }
    }
    if (selected.empty()) {
        MessageBoxW(state->hwnd, L"Select at least one site first.",
                    L"Nothing selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring msg = L"Delete cached data for " + std::to_wstring(selected.size()) +
                       L" site(s), about " + FormatBytes(total) + L"?\n\n"
                       L"The sites will re-download their cache on the next visit.";
    std::wstring running = FindRunningProcess(GetCategoryProcessNames(CleanupCategory::BrowserSWCache));
    if (!running.empty()) {
        msg += L"\n\nThe browser is still running (" + running +
               L"). Close it first for the most reliable result.";
    }
    if (MessageBoxW(state->hwnd, msg.c_str(), L"Confirm",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    int64_t freed = 0;
    int failed = 0;
    for (size_t idx : selected) {
        const std::wstring path = state->sites[idx].bucketPath;
        if (!IsSafeDeleteRoot(path)) {
            ++failed;
            continue;
        }
        if (RemoveTreeForce(path)) {
            freed += state->sites[idx].size;
        } else {
            ++failed;
        }
    }

    std::wstring extra = L"Freed " + FormatBytes(freed);
    if (failed > 0) extra += L", " + std::to_wstring(failed) + L" failed (in use)";
    LoadData(state);
    FillList(state);
    SetHeaderText(state, extra);
}

void CreateChildControls(DetailsState* state) {
    int dpi = state->dpi;
    auto sc = [dpi](int v) { return MulDiv(v, dpi, 96); };

    state->header = CreateWindowExW(0, WC_STATICW, L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 0, 0, state->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DETAILS_HEADER)),
        GetModuleHandleW(nullptr), nullptr);

    DWORD listStyle = WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
    state->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        listStyle, 0, 0, 0, 0, state->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DETAILS_LIST)),
        GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(state->list,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
        (state->siteMode ? LVS_EX_CHECKBOXES : 0));

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.pszText = const_cast<LPWSTR>(state->siteMode ? L"Site" : L"Largest entries");
    col.cx = sc(380);
    col.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(state->list, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Last used");
    col.cx = sc(120);
    col.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(state->list, 1, &col);

    col.pszText = const_cast<LPWSTR>(L"Size");
    col.cx = sc(110);
    col.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(state->list, 2, &col);

    state->deleteButton = CreateWindowExW(0, WC_BUTTONW,
        state->siteMode ? L"Delete selected sites" : L"Close",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, state->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DETAILS_DELETE)),
        GetModuleHandleW(nullptr), nullptr);

    state->closeButton = CreateWindowExW(0, WC_BUTTONW, L"Close",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, state->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DETAILS_CLOSE)),
        GetModuleHandleW(nullptr), nullptr);
    if (!state->siteMode) ShowWindow(state->deleteButton, SW_HIDE);

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(state->parent, WM_GETFONT, 0, 0));
    if (font) {
        SendMessageW(state->header, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(state->list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(state->deleteButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(state->closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void LayoutChildren(DetailsState* state, int width, int height) {
    auto sc = [state](int v) { return ScaleOf(state, v); };
    const int pad = sc(10);
    const int headerH = sc(28);
    const int buttonH = sc(34);
    const int buttonW = sc(170);
    const int contentW = width - pad * 2 > sc(50) ? width - pad * 2 : sc(50);

    int y = pad;
    MoveWindow(state->header, pad, y, contentW, headerH, TRUE);
    y += headerH + sc(4);

    int listH = height - y - buttonH - pad * 3;
    if (listH < sc(80)) listH = sc(80);
    MoveWindow(state->list, pad, y, contentW, listH, TRUE);
    y += listH + pad;

    MoveWindow(state->deleteButton, pad, y, buttonW, buttonH, TRUE);
    MoveWindow(state->closeButton, width - pad - sc(110), y, sc(110), buttonH, TRUE);
}

LRESULT CALLBACK DetailsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<DetailsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<DetailsState*>(create->lpCreateParams);
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            CreateChildControls(state);
            SetWindowTextW(state->header, (state->categoryName + L" - analyzing...").c_str());
            RECT rc;
            GetClientRect(hwnd, &rc);
            LayoutChildren(state, rc.right, rc.bottom);
            // 大目录统计耗时较长，等窗口显示后再加载，避免启动卡顿
            PostMessageW(hwnd, WM_DETAILS_LOADED, 0, 0);
            return 0;
        }
        case WM_DETAILS_LOADED:
            if (state && IsWindow(state->list)) {
                LoadData(state);
                FillList(state);
            }
            return 0;
        case WM_SIZE:
            if (state) LayoutChildren(state, LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            if (!state) break;
            if (LOWORD(wParam) == IDC_DETAILS_DELETE) {
                DeleteSelected(state);
                return 0;
            }
            if (LOWORD(wParam) == IDC_DETAILS_CLOSE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            // 只通知本线程的模态循环退出；不能用 PostQuitMessage，
            // 否则会连带结束主窗口的消息循环，表现为整个程序退出。
            PostThreadMessageW(GetCurrentThreadId(), WM_DETAILS_CLOSED, 0, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void ShowCategoryDetails(HWND parent, HINSTANCE instance,
                         CleanupCategory category, const std::wstring& categoryName,
                         int dpi) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DetailsWndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = DETAILS_WINDOW_CLASS;
        if (!RegisterClassExW(&wc)) return;
        classRegistered = true;
    }

    DetailsState state;
    state.parent = parent;
    state.category = category;
    state.categoryName = categoryName;
    state.dpi = dpi > 0 ? dpi : 96;
    state.siteMode = (category == CleanupCategory::BrowserSWCache);

    RECT parentRect = {};
    GetWindowRect(parent, &parentRect);
    int width = MulDiv(760, state.dpi, 96);
    int height = MulDiv(520, state.dpi, 96);
    int x = parentRect.left + MulDiv(40, state.dpi, 96);
    int y = parentRect.top + MulDiv(40, state.dpi, 96);

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        DETAILS_WINDOW_CLASS,
        (L"Details - " + categoryName).c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, width, height,
        parent, nullptr, instance, &state);

    if (!hwnd) return;

    EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_DETAILS_CLOSED) break;
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetActiveWindow(parent);
    }
}

} // namespace diskcleaner
