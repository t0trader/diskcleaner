#pragma once

#include "types.h"

#include <atomic>
#include <string>
#include <vector>

namespace diskcleaner {

// 类别关联的进程名（不含 .exe）。类别与进程无关时返回空。
std::vector<std::wstring> GetCategoryProcessNames(CleanupCategory category);

// 在 names 中查找正在运行的进程，返回首个命中的进程名；都没有运行时返回空串。
std::wstring FindRunningProcess(const std::vector<std::wstring>& names);

// 由系统服务托管、需要停服后整体清理的类别：不做时间过滤，
// 否则会残留新旧混合的数据库文件，且扫描大小与实际可删量不一致。
bool IsServiceManagedCategory(CleanupCategory category);

// 统计目录中 mtime 早于阈值（小时）的文件；thresholdHours<=0 表示不过滤。
int64_t GetDirectorySizeFiltered(const std::wstring& path, int thresholdHours,
                                 int64_t* fileCount, const std::atomic<bool>* cancel);

// 单个文件是否足够旧（可删除）。访问失败时按“足够旧”处理，由删除阶段决定成败。
bool IsFileOlderThan(const std::wstring& path, int thresholdHours);

} // namespace diskcleaner
