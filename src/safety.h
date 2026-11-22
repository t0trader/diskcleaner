#pragma once

#include <string>

namespace diskcleaner {

// 受保护的用户数据文件/目录名（大小写不敏感）。出现在任何缓存目录内都不允许删除，
// 例如 Chromium 的 Login Data / Local Storage / IndexedDB、Outlook 的 .ost 等。
bool IsProtectedDataName(const std::wstring& leaf);

// 拒绝删除的根路径（盘符根、用户目录根、Windows 目录等），防止路径表写错造成误删。
bool IsSafeDeleteRoot(const std::wstring& path);

// path 是否等于 root 或位于 root 之下（大小写不敏感，忽略末尾反斜杠）。
bool PathEqualsOrUnder(const std::wstring& path, const std::wstring& root);

} // namespace diskcleaner
