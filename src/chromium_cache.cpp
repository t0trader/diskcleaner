#include "chromium_cache.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cwctype>
#include <chrono>
#include <limits>

namespace fs = std::filesystem;

namespace diskcleaner {

namespace {

constexpr int64_t kMaxIndexBytes = 256 * 1024;

bool IsUrlChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ||
           c == ':' || c == '/' || c == '%';
}

// 从一段字节中提取第一个 http(s) origin（scheme://host[:port]）
std::wstring ParseOriginFromBuffer(const std::string& data) {
    for (size_t i = 0; i + 7 < data.size(); ++i) {
        if (data.compare(i, 8, "https://") == 0 || data.compare(i, 7, "http://") == 0) {
            const size_t start = i;
            size_t end = i;
            while (end < data.size() && IsUrlChar(data[end])) ++end;
            std::string url = data.substr(start, end - start);
            // 去掉路径部分，只保留 scheme://host[:port]
            const size_t schemeEnd = url.find("://");
            if (schemeEnd != std::string::npos) {
                const size_t pathStart = url.find('/', schemeEnd + 3);
                if (pathStart != std::string::npos) url.resize(pathStart);
            }
            while (!url.empty() && (url.back() == '/' || url.back() == '\\')) url.pop_back();
            return std::wstring(url.begin(), url.end());
        }
    }
    return {};
}

// index.txt 是 Chromium 的序列化结构，其中包含形如 https://host/ 的 origin 明文。
// 这里按字节扫描第一个 http(s) URL。
std::wstring ParseOriginFromIndex(const std::wstring& indexPath) {
    std::ifstream in(fs::path(indexPath), std::ios::binary);
    if (!in) return {};

    std::string data;
    data.resize(kMaxIndexBytes);
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(in.gcount()));
    return ParseOriginFromBuffer(data);
}

// 部分 Electron 应用的 index.txt 只记录缓存名、不含域名。
// 此时从缓存条目开头抽样读取请求 URL，取出现次数最多的站点域名。
std::wstring GuessOriginFromEntries(const std::wstring& bucketDir) {
    const size_t kSampleFiles = 24;
    const size_t kHeadBytes = 4096;

    std::vector<std::wstring> hosts;
    std::error_code ec;
    fs::recursive_directory_iterator it(fs::path(bucketDir),
        fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end && hosts.size() < kSampleFiles) {
        std::error_code entryEc;
        try {
            if (it->is_regular_file(entryEc)) {
                std::ifstream in(it->path(), std::ios::binary);
                if (in) {
                    std::string head(kHeadBytes, '\0');
                    in.read(head.data(), static_cast<std::streamsize>(head.size()));
                    head.resize(static_cast<size_t>(in.gcount()));
                    std::wstring origin = ParseOriginFromBuffer(head);
                    if (!origin.empty()) hosts.push_back(origin);
                }
            }
        } catch (...) {}
        it.increment(ec);
    }

    if (hosts.empty()) return {};

    // 头部里的 w3.org / adobe 等命名空间不是站点，排除后取众数
    std::sort(hosts.begin(), hosts.end());
    std::wstring best;
    size_t bestCount = 0;
    for (size_t i = 0; i < hosts.size();) {
        size_t j = i;
        while (j < hosts.size() && hosts[j] == hosts[i]) ++j;
        const size_t count = j - i;
        const std::wstring& candidate = hosts[i];
        const bool noise = candidate.find(L"w3.org") != std::wstring::npos ||
                           candidate.find(L"adobe.com") != std::wstring::npos ||
                           candidate.find(L"purl.org") != std::wstring::npos ||
                           candidate.find(L"xmlsoap.org") != std::wstring::npos;
        if (!noise && count > bestCount) {
            bestCount = count;
            best = candidate;
        }
        i = j;
    }
    return best;
}

int64_t SecondsSince(fs::file_time_type t) {
    auto age = fs::file_time_type::clock::now() - t;
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(age).count();
    return secs < 0 ? 0 : static_cast<int64_t>(secs);
}

struct TreeStats {
    int64_t size = 0;
    int64_t newestAge = std::numeric_limits<int64_t>::max();  // 最近一次写入距今秒数
};

TreeStats MeasureTree(const std::wstring& root, const std::atomic<bool>* cancel) {
    TreeStats stats;
    std::error_code ec;
    fs::recursive_directory_iterator it(fs::path(root),
        fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (cancel && cancel->load()) break;
        std::error_code entryEc;
        try {
            if (it->is_symlink(entryEc)) {
                it.disable_recursion_pending();
            } else if (it->is_regular_file(entryEc)) {
                std::error_code sizeEc;
                auto size = it->file_size(sizeEc);
                if (!sizeEc) {
                    stats.size += static_cast<int64_t>(size);
                }
                std::error_code timeEc;
                auto lastWrite = it->last_write_time(timeEc);
                if (!timeEc) {
                    int64_t age = SecondsSince(lastWrite);
                    if (age < stats.newestAge) stats.newestAge = age;
                }
            }
        } catch (...) {}
        it.increment(ec);
    }
    if (stats.newestAge == std::numeric_limits<int64_t>::max()) stats.newestAge = 0;
    return stats;
}

} // namespace

std::wstring FormatAgeText(int64_t ageSeconds) {
    if (ageSeconds < 3600) return L"<1h";
    const int64_t hours = ageSeconds / 3600;
    if (hours < 24) return std::to_wstring(hours) + L"h ago";
    const int64_t days = hours / 24;
    if (days < 30) return std::to_wstring(days) + L"d ago";
    const int64_t months = days / 30;
    if (months < 12) return std::to_wstring(months) + L"mo ago";
    return std::to_wstring(days / 365) + L"y ago";
}

std::vector<SiteCacheEntry> AnalyzeCacheStorage(const std::wstring& cacheStorageDir,
                                                const std::atomic<bool>* cancel) {
    std::vector<SiteCacheEntry> result;
    std::error_code ec;
    fs::directory_iterator it(fs::path(cacheStorageDir),
        fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    while (!ec && it != end) {
        if (cancel && cancel->load()) break;
        std::error_code entryEc;
        try {
            if (!it->is_directory(entryEc)) {
                it.increment(ec);
                continue;
            }
        } catch (...) {
            it.increment(ec);
            continue;
        }

        SiteCacheEntry entry;
        entry.bucketPath = it->path().wstring();
        std::wstring index = entry.bucketPath + L"\\index.txt";
        entry.origin = ParseOriginFromIndex(index);
        if (entry.origin.empty()) {
            entry.origin = GuessOriginFromEntries(entry.bucketPath);
        }
        if (entry.origin.empty()) {
            entry.origin = L"(unknown origin)";
        }
        TreeStats stats = MeasureTree(entry.bucketPath, cancel);
        entry.size = stats.size;
        entry.ageSeconds = stats.newestAge;
        entry.lastUsedText = FormatAgeText(entry.ageSeconds);
        result.push_back(std::move(entry));

        it.increment(ec);
    }

    std::sort(result.begin(), result.end(),
        [](const SiteCacheEntry& a, const SiteCacheEntry& b) { return a.size > b.size; });
    return result;
}

std::vector<TopEntry> GetTopEntries(const std::wstring& root, int limit,
                                    const std::atomic<bool>* cancel) {
    std::vector<TopEntry> entries;
    std::error_code ec;
    fs::directory_iterator it(fs::path(root),
        fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    while (!ec && it != end) {
        if (cancel && cancel->load()) break;
        std::error_code entryEc;
        try {
            if (it->is_symlink(entryEc)) {
                it.increment(ec);
                continue;
            }

            TopEntry entry;
            entry.path = it->path().wstring();
            entry.name = it->path().filename().wstring();
            entry.isDirectory = it->is_directory(entryEc);
            if (entry.isDirectory) {
                TreeStats stats = MeasureTree(entry.path, cancel);
                entry.size = stats.size;
                entry.ageSeconds = stats.newestAge;
            } else {
                std::error_code sizeEc;
                auto size = it->file_size(sizeEc);
                entry.size = sizeEc ? 0 : static_cast<int64_t>(size);
                std::error_code timeEc;
                auto lastWrite = it->last_write_time(timeEc);
                entry.ageSeconds = timeEc ? 0 : SecondsSince(lastWrite);
            }
            entry.lastUsedText = FormatAgeText(entry.ageSeconds);
            entries.push_back(std::move(entry));
        } catch (...) {}
        it.increment(ec);
    }

    std::sort(entries.begin(), entries.end(),
        [](const TopEntry& a, const TopEntry& b) { return a.size > b.size; });
    if (limit > 0 && static_cast<int>(entries.size()) > limit) {
        entries.resize(static_cast<size_t>(limit));
    }
    return entries;
}

} // namespace diskcleaner
