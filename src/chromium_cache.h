#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace diskcleaner {

// Chromium CacheStorage 中单个站点（origin）占用的离线缓存
struct SiteCacheEntry {
    std::wstring origin;      // 例如 https://www.mgtv.com
    std::wstring bucketPath;  // 该站点的哈希目录
    int64_t size = 0;
    int64_t ageSeconds = 0;   // 最近写入距今秒数
    std::wstring lastUsedText;
};

// 某目录下最大的若干子项
struct TopEntry {
    std::wstring name;
    std::wstring path;
    int64_t size = 0;
    int64_t ageSeconds = 0;
    std::wstring lastUsedText;
    bool isDirectory = false;
};

std::wstring FormatAgeText(int64_t ageSeconds);

// 解析 CacheStorage 下每个哈希目录的 index.txt，得到站点域名与占用大小，按大小降序。
std::vector<SiteCacheEntry> AnalyzeCacheStorage(const std::wstring& cacheStorageDir,
                                                const std::atomic<bool>* cancel = nullptr);

// 返回 root 下最大的 limit 个子项（递归统计大小）。
std::vector<TopEntry> GetTopEntries(const std::wstring& root, int limit,
                                    const std::atomic<bool>* cancel = nullptr);

} // namespace diskcleaner
