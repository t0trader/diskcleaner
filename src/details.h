#pragma once

#include <windows.h>
#include "types.h"

namespace diskcleaner {

// 打开“详情”窗口：BrowserSWCache 显示站点占用榜并支持按站点删除；
// 其他类别显示 Top 大文件/子目录（只读）。
void ShowCategoryDetails(HWND parent, HINSTANCE instance,
                         CleanupCategory category, const std::wstring& categoryName,
                         int dpi);

} // namespace diskcleaner
