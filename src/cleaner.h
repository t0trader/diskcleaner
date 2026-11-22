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
    // 智能策略跳过的内容：仍在运行的应用类别、受保护文件、未到时间阈值的文件
    int64_t GetSkippedNewerFiles() const { return skippedNewer_.load(); }
    int64_t GetSkippedProtectedFiles() const { return skippedProtected_.load(); }
    int GetSkippedRunningApps() const { return skippedRunningApps_.load(); }

    void SetProgressCallback(CleanupProgressCallback callback) { std::lock_guard<std::mutex> lock(mutex_); progressCallback_ = std::move(callback); }
    void SetCompleteCallback(CleanupCompleteCallback callback) { std::lock_guard<std::mutex> lock(mutex_); completeCallback_ = std::move(callback); }
    void SetOptions(const CleanupOptions& options);
    CleanupOptions GetOptions() const;

private:
    void CleanupWorker(std::vector<CleanupItem> items);
    bool CleanupCategoryItem(const CleanupItem& item, const CleanupProgressCallback& progressCb);
    // 递归清理目录内容：只删超过时间阈值的文件与受保护名单之外的内容，
    // 不删除 root 本身，空子目录在清理后移除。
    bool CleanupTree(const std::wstring& root, CleanupCategory category,
                     const CleanupProgressCallback& progressCb, int depth = 0);
    bool CleanupFile(const std::wstring& path, CleanupCategory category);

    std::atomic<bool> cleaning_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<int64_t> bytesFreed_{0};
    std::atomic<int64_t> filesDeleted_{0};
    std::atomic<int64_t> filesFailed_{0};
    std::atomic<int64_t> skippedNewer_{0};
    std::atomic<int64_t> skippedProtected_{0};
    std::atomic<int> skippedRunningApps_{0};
    std::thread cleanupThread_;
    std::mutex mutex_;
    mutable std::mutex optionsMutex_;
    CleanupOptions options_;

    CleanupProgressCallback progressCallback_;
    CleanupCompleteCallback completeCallback_;
};

} // namespace diskcleaner
