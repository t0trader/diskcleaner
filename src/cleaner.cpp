#include "cleaner.h"
#include "category_paths.h"
#include "safety.h"
#include "smart.h"
#include "utils.h"
#include <filesystem>
#include <chrono>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace diskcleaner {

namespace {

// 删除单个文件；只读文件清除属性后重试，失败则恢复原属性。
bool DeleteFileWithRetry(const std::wstring& path) {
    std::error_code ec;
    if (fs::remove(path, ec)) return true;

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_READONLY)) {
        return false;
    }
    SetFileAttributesW(path.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    ec.clear();
    if (fs::remove(path, ec)) return true;

    SetFileAttributesW(path.c_str(), attrs);
    return false;
}

int64_t FileSizeSafe(const std::wstring& path) {
    try {
        return fs::file_size(path);
    } catch (...) {
        return 0;
    }
}

// 无文件大小但必须执行系统操作的类别
bool IsCommandCategory(CleanupCategory category) {
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

int EffectiveThresholdHours(const CleanupOptions& options, CleanupCategory category) {
    // Prefetch 无论 Smart mode 是否开启都至少保留 24 小时，与扫描规则保持一致
    if (category == CleanupCategory::Prefetch) {
        if (!options.smartMode) return 24;
        return options.ageThresholdHours > 24 ? options.ageThresholdHours : 24;
    }
    if (!options.smartMode) return 0;
    return options.ageThresholdHours;
}

} // namespace

Cleaner::Cleaner() {}

Cleaner::~Cleaner() {
    StopCleanup();
}

void Cleaner::SetOptions(const CleanupOptions& options) {
    std::lock_guard<std::mutex> lock(optionsMutex_);
    options_ = options;
}

CleanupOptions Cleaner::GetOptions() const {
    std::lock_guard<std::mutex> lock(optionsMutex_);
    return options_;
}

void Cleaner::StartCleanup(const std::vector<CleanupItem>& items) {
    // If already cleaning, stop first
    if (cleaning_.load()) {
        StopCleanup();
    }

    // Wait for previous thread to finish
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }

    stopRequested_ = false;
    cleaning_ = true;
    bytesFreed_ = 0;
    filesDeleted_ = 0;
    filesFailed_ = 0;
    skippedNewer_ = 0;
    skippedProtected_ = 0;
    skippedRunningApps_ = 0;

    const CleanupOptions options = GetOptions();

    std::vector<CleanupItem> enabledItems;
    for (const auto& item : items) {
        if (!item.enabled || !item.scanned) continue;
        // 跳过扫描结果为空的文件类类别，避免无意义地停止/重启系统服务
        if (item.size == 0 && !IsCommandCategory(item.category)) continue;
        // 应用仍在运行：整类跳过，避免删除其正在使用的缓存（与时间闸门相互独立）
        if (options.skipRunningApps) {
            auto procs = GetCategoryProcessNames(item.category);
            if (!procs.empty() && !FindRunningProcess(procs).empty()) {
                ++skippedRunningApps_;
                continue;
            }
        }
        enabledItems.push_back(item);
    }

    cleanupThread_ = std::thread(&Cleaner::CleanupWorker, this, std::move(enabledItems));
}

void Cleaner::StopCleanup() {
    stopRequested_ = true;
    cleaning_ = false;
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }
}

void Cleaner::CleanupWorker(std::vector<CleanupItem> items) {
    // Copy callbacks under lock
    CleanupProgressCallback progressCb;
    CleanupCompleteCallback completeCb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progressCb = progressCallback_;
        completeCb = completeCallback_;
    }

    for (const auto& item : items) {
        if (stopRequested_) break;
        CleanupCategoryItem(item, progressCb);
    }

    // 先落状态再回调，保证 UI 收到完成消息时 IsCleaning() 已为 false；
    // StartCleanup 会 join 旧线程，因此提前清标志不会造成并发清理。
    cleaning_ = false;

    bool shouldCallComplete = !stopRequested_ && completeCb;
    if (shouldCallComplete) {
        completeCb(bytesFreed_.load(), filesDeleted_.load());
    }
}

bool Cleaner::CleanupCategoryItem(const CleanupItem& item, const CleanupProgressCallback& progressCb) {
    if (item.category == CleanupCategory::RecycleBin) {
        if (progressCb) {
            progressCb(item.category, L"Emptying Recycle Bin...", 0);
        }
        if (EmptyRecycleBin()) {
            bytesFreed_ += item.size;
            filesDeleted_ += item.fileCount;
            return true;
        }
        return false;
    }

    // Special command-based cleanup categories
    switch (item.category) {
        case CleanupCategory::HibernationFile: {
            if (progressCb) progressCb(item.category, L"Disabling hibernation...", 0);
            if (!DisableHibernate()) return false;
            // powercfg 删除 hiberfil.sys 后按扫描大小计入释放量
            bytesFreed_ += item.size;
            filesDeleted_ += item.fileCount;
            return true;
        }
        case CleanupCategory::EventLogsWEvtUtil: {
            if (progressCb) progressCb(item.category, L"Clearing event logs...", 0);
            return ClearEventLogs();
        }
        case CleanupCategory::SearchIndex: {
            if (progressCb) progressCb(item.category, L"Clearing search index cache...", 0);
            // 等待 WSearch 服务真正停止后再删除索引文件，避免文件被占用
            StopServiceAndWait(L"WSearch");
            bool success = true;
            for (const auto& p : GetCategoryPaths(item.category)) {
                if (DirectoryExists(p)) {
                    success &= CleanupTree(p, item.category, progressCb);
                } else if (FileExists(p)) {
                    success &= CleanupFile(p, item.category);
                }
            }
            StartServiceByName(L"WSearch");
            return success;
        }
        case CleanupCategory::FontCache: {
            if (progressCb) progressCb(item.category, L"Clearing font cache...", 0);
            StopServiceAndWait(L"FontCache");
            bool success = true;
            for (const auto& p : GetCategoryPaths(item.category)) {
                if (DirectoryExists(p)) {
                    success &= CleanupTree(p, item.category, progressCb);
                }
            }
            StartServiceByName(L"FontCache");
            return success;
        }
        case CleanupCategory::WindowsUpdate: {
            if (progressCb) progressCb(item.category, L"Clearing Windows Update cache...", 0);
            // 等待服务完全停止，避免 SoftwareDistribution 文件被占用
            StopServiceAndWait(L"wuauserv");
            StopServiceAndWait(L"bits");
            bool success = true;
            for (const auto& p : GetCategoryPaths(item.category)) {
                if (DirectoryExists(p)) {
                    success &= CleanupTree(p, item.category, progressCb);
                }
            }
            // 同时清理 SoftwareDistribution 根目录下的残留数据
            std::wstring root = GetWindowsDir() + L"\\SoftwareDistribution";
            if (DirectoryExists(root)) {
                success &= CleanupTree(root, item.category, progressCb);
            }
            StartServiceByName(L"wuauserv");
            StartServiceByName(L"bits");
            return success;
        }
        case CleanupCategory::PrintSpooler: {
            if (progressCb) progressCb(item.category, L"Clearing print spooler...", 0);
            // 停止后台打印服务后再清理假脱机文件，完成后重新启动
            StopServiceAndWait(L"Spooler");
            std::wstring spool = GetSystemDir() + L"\\spool\\PRINTERS";
            bool success = DirectoryExists(spool)
                ? CleanupTree(spool, item.category, progressCb)
                : true;
            StartServiceByName(L"Spooler");
            return success;
        }
        case CleanupCategory::MemoryDump: {
            if (progressCb) progressCb(item.category, L"Clearing memory dump files...", 0);
            std::wstring winDir = GetWindowsDir();
            std::wstring dumpPaths[] = {
                winDir + L"\\MEMORY.DMP",
                winDir + L"\\Minidump",
                winDir + L"\\LiveKernelReports"
            };
            // 逐项清理并统计，避免再叠加扫描大小导致重复计数
            for (const auto& dp : dumpPaths) {
                if (DirectoryExists(dp)) {
                    CleanupTree(dp, item.category, progressCb);
                } else {
                    CleanupFile(dp, item.category);
                }
            }
            return true;
        }
        case CleanupCategory::CrashDumpSettings: {
            // 关闭崩溃转储与 WER（可通过系统属性重新开启）
            if (progressCb) progressCb(item.category, L"Disabling crash dump settings...", 0);
            const DWORD none = 0;
            const DWORD enabledValue = 1;
            bool ok = SetRegistryValue(L"SYSTEM\\CurrentControlSet\\Control\\CrashControl",
                    L"CrashDumpEnabled", REG_DWORD,
                    reinterpret_cast<const BYTE*>(&none), sizeof(none));
            ok &= SetRegistryValue(L"SYSTEM\\CurrentControlSet\\Control\\CrashControl",
                    L"LogEvent", REG_DWORD,
                    reinterpret_cast<const BYTE*>(&none), sizeof(none));
            ok &= SetRegistryValue(L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting",
                    L"Disabled", REG_DWORD,
                    reinterpret_cast<const BYTE*>(&enabledValue), sizeof(enabledValue));
            return ok;
        }
        case CleanupCategory::FlushDNS: {
            if (progressCb) progressCb(item.category, L"Flushing DNS cache...", 0);
            return FlushDNSCache();
        }
        default:
            break;
    }

    for (const auto& path : GetCategoryPaths(item.category)) {
        if (stopRequested_) break;
        if (DirectoryExists(path)) {
            CleanupTree(path, item.category, progressCb);
        } else if (FileExists(path)) {
            CleanupFile(path, item.category);
        }
    }
    return true;
}

bool Cleaner::CleanupTree(const std::wstring& root, CleanupCategory category,
                          const CleanupProgressCallback& progressCb, int depth) {
    if (depth > 64 || stopRequested_) return false;

    // 根路径安全校验：拒绝盘符根、用户目录根、Windows 目录、目录联接等
    if (!IsSafeDeleteRoot(root)) {
        ++filesFailed_;
        return false;
    }

    const CleanupOptions options = GetOptions();
    const int thresholdHours = IsServiceManagedCategory(category)
        ? 0
        : EffectiveThresholdHours(options, category);
    const bool filterThumbnails = (category == CleanupCategory::ThumbnailCache);

    // 先快照子项再处理：删除子项会使目录迭代器失效
    std::vector<fs::directory_entry> children;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::path(root),
            fs::directory_options::skip_permission_denied, ec)) {
        children.push_back(entry);
    }

    ULONGLONG lastReport = 0;
    for (const auto& entry : children) {
        if (stopRequested_) return false;

        std::error_code entryEc;
        try {
            if (entry.is_symlink(entryEc)) continue;

            const std::wstring entryPath = entry.path().wstring();
            const std::wstring leaf = entry.path().filename().wstring();

            ULONGLONG now = GetTickCount64();
            if (progressCb && now - lastReport >= 100) {
                lastReport = now;
                progressCb(category, entryPath, bytesFreed_.load());
            }

            // 用户数据护栏：Login Data / Cookies / IndexedDB / Local Storage 等永不删除
            if (IsProtectedDataName(leaf)) {
                ++skippedProtected_;
                continue;
            }

            // CacheStorage 的 index.txt 是站点注册索引，保留它可让浏览器继续复用该站点缓存
            if (category == CleanupCategory::BrowserSWCache &&
                _wcsicmp(leaf.c_str(), L"index.txt") == 0) {
                continue;
            }

            if (entry.is_directory(entryEc)) {
                CleanupTree(entryPath, category, progressCb, depth + 1);
                // 只移除已被清空的子目录，保留仍含新文件/受保护文件的目录
                RemoveDirectoryW(entryPath.c_str());
                continue;
            }

            if (filterThumbnails &&
                leaf.find(L"thumbcache") == std::wstring::npos &&
                leaf.find(L"iconcache") == std::wstring::npos) {
                continue;
            }

            if (!IsFileOlderThan(entryPath, thresholdHours)) {
                ++skippedNewer_;
                continue;
            }

            int64_t entrySize = FileSizeSafe(entryPath);
            if (DeleteFileWithRetry(entryPath)) {
                bytesFreed_ += entrySize;
                ++filesDeleted_;
            } else {
                ++filesFailed_;
            }
        } catch (...) {
            ++filesFailed_;
        }
    }
    return true;
}

bool Cleaner::CleanupFile(const std::wstring& path, CleanupCategory category) {
    if (!FileExists(path)) return false;
    if (!IsSafeDeleteRoot(path)) {
        ++filesFailed_;
        return false;
    }

    const std::wstring leaf = fs::path(path).filename().wstring();
    if (IsProtectedDataName(leaf)) {
        ++skippedProtected_;
        return false;
    }

    const CleanupOptions options = GetOptions();
    const int thresholdHours = IsServiceManagedCategory(category)
        ? 0
        : EffectiveThresholdHours(options, category);
    if (!IsFileOlderThan(path, thresholdHours)) {
        ++skippedNewer_;
        return false;
    }

    int64_t size = FileSizeSafe(path);
    if (DeleteFileWithRetry(path)) {
        bytesFreed_ += size;
        ++filesDeleted_;
        return true;
    }
    ++filesFailed_;
    return false;
}

} // namespace diskcleaner
