#pragma once

#include "types.h"
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace diskcleaner {

using ScanProgressCallback = std::function<void(CleanupCategory category, const std::wstring& currentPath)>;
using ScanCompleteCallback = std::function<void(const std::vector<CleanupItem>& items)>;

class Scanner {
public:
    Scanner();
    ~Scanner();

    void StartScan();
    void StopScan();
    bool IsScanning() const { return scanning_.load(); }
    std::vector<CleanupItem> GetResults() const;
    int64_t GetTotalSize() const;
    int64_t GetTotalFileCount() const;

    void SetProgressCallback(ScanProgressCallback callback) { std::lock_guard<std::mutex> lock(resultsMutex_); progressCallback_ = std::move(callback); }
    void SetCompleteCallback(ScanCompleteCallback callback) { std::lock_guard<std::mutex> lock(resultsMutex_); completeCallback_ = std::move(callback); }
    void SetItemEnabled(CleanupCategory category, bool enabled);

    static std::wstring GetCategoryName(CleanupCategory category);
    static std::wstring GetCategoryDescription(CleanupCategory category);
    static RiskLevel GetCategoryRisk(CleanupCategory category);
    static std::wstring GetRiskLevelName(RiskLevel level);

private:
    void ScanWorker();
    CleanupItem ScanCategory(CleanupCategory category);
    std::vector<std::wstring> GetCategoryPaths(CleanupCategory category);

    std::vector<CleanupItem> results_;
    mutable std::mutex resultsMutex_;
    std::atomic<bool> scanning_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread scanThread_;

    ScanProgressCallback progressCallback_;
    ScanCompleteCallback completeCallback_;
};

} // namespace diskcleaner