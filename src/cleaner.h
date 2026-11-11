#pragma once

#include "types.h"
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace diskcleaner {

using CleanupProgressCallback = std::function<void(CleanupCategory category, const std::wstring& currentPath, int64_t bytesFreed)>;
using CleanupCompleteCallback = std::function<void(int64_t totalBytesFreed, int64_t totalFilesDeleted)>;

class Cleaner {
public:
    Cleaner();
    ~Cleaner();

    void StartCleanup(const std::vector<CleanupItem>& items);
    void StopCleanup();
    bool IsCleaning() const { return cleaning_.load(); }
    int64_t GetBytesFreed() const { return bytesFreed_.load(); }
    int64_t GetFilesDeleted() const { return filesDeleted_.load(); }
    int64_t GetFilesFailed() const { return filesFailed_.load(); }

    void SetProgressCallback(CleanupProgressCallback callback) { std::lock_guard<std::mutex> lock(mutex_); progressCallback_ = std::move(callback); }
    void SetCompleteCallback(CleanupCompleteCallback callback) { std::lock_guard<std::mutex> lock(mutex_); completeCallback_ = std::move(callback); }

private:
    void CleanupWorker(std::vector<CleanupItem> items);
    bool CleanupCategoryItem(const CleanupItem& item, const CleanupProgressCallback& progressCb);
    bool CleanupDirectory(const std::wstring& path, CleanupCategory category, const CleanupProgressCallback& progressCb);
    bool CleanupFile(const std::wstring& path, CleanupCategory category);

    std::atomic<bool> cleaning_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<int64_t> bytesFreed_{0};
    std::atomic<int64_t> filesDeleted_{0};
    std::atomic<int64_t> filesFailed_{0};
    std::thread cleanupThread_;
    std::mutex mutex_;

    CleanupProgressCallback progressCallback_;
    CleanupCompleteCallback completeCallback_;
};

} // namespace diskcleaner