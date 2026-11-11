#include "cleaner.h"
#include "category_paths.h"
#include "utils.h"
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

namespace diskcleaner {

namespace {

// 清除目录树中文件的只读属性（不跟踪符号链接），用于删除失败后的重试。
// 只读属性会阻止 DeleteFile，只清除不会改变目录结构。
void ClearReadOnlyRecursive(const std::wstring& root) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root,
        fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        std::error_code entryEc;
        if (it->is_symlink(entryEc)) {
            it.disable_recursion_pending();
        } else {
            DWORD attrs = GetFileAttributesW(it->path().c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesW(it->path().c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
            }
        }
        it.increment(ec);
    }
}

// 删除目录树；失败时清除只读属性后重试一次。
bool RemoveTree(const std::wstring& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (!ec) return true;

    ClearReadOnlyRecursive(path);
    ec.clear();
    fs::remove_all(path, ec);
    if (ec) return false;

    std::error_code existsEc;
    return !fs::exists(path, existsEc);
}

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

} // namespace

Cleaner::Cleaner() {}

Cleaner::~Cleaner() {
    StopCleanup();
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

    std::vector<CleanupItem> enabledItems;
    for (const auto& item : items) {
        if (!item.enabled || !item.scanned) continue;
        // 跳过扫描结果为空的文件类类别，避免无意义地停止/重启系统服务
        if (item.size == 0 && !IsCommandCategory(item.category)) continue;
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
                    success &= CleanupDirectory(p, item.category, progressCb);
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
                    success &= CleanupDirectory(p, item.category, progressCb);
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
                    success &= CleanupDirectory(p, item.category, progressCb);
                }
            }
            // 同时清理 SoftwareDistribution 根目录下的残留数据
            std::wstring root = GetWindowsDir() + L"\\SoftwareDistribution";
            if (DirectoryExists(root)) {
                success &= CleanupDirectory(root, item.category, progressCb);
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
                ? CleanupDirectory(spool, item.category, progressCb)
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
                    CleanupDirectory(dp, item.category, progressCb);
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
            CleanupDirectory(path, item.category, progressCb);
        } else if (FileExists(path)) {
            CleanupFile(path, item.category);
        }
    }
    return true;
}

bool Cleaner::CleanupDirectory(const std::wstring& path, CleanupCategory category, const CleanupProgressCallback& progressCb) {
    // 进度节流：每个目录最多每 100ms 回调一次，避免大量小文件时刷爆消息队列
    ULONGLONG lastReport = 0;
    try {
        for (const auto& entry : fs::directory_iterator(path,
                fs::directory_options::skip_permission_denied)) {
            if (stopRequested_) return false;
            try {
                // 跳过符号链接/目录联接，避免循环遍历和误删目标文件
                if (entry.is_symlink()) continue;

                std::wstring entryPath = entry.path().wstring();
                ULONGLONG now = GetTickCount64();
                if (progressCb && now - lastReport >= 100) {
                    lastReport = now;
                    progressCb(category, entryPath, bytesFreed_.load());
                }

                if (category == CleanupCategory::Prefetch) {
                    auto lastWrite = entry.last_write_time();
                    auto age = fs::file_time_type::clock::now() - lastWrite;
                    if (age < std::chrono::hours(24)) continue;
                }

                if (category == CleanupCategory::ThumbnailCache) {
                    std::wstring filename = entry.path().filename().wstring();
                    if (filename.find(L"thumbcache") == std::wstring::npos &&
                        filename.find(L"iconcache") == std::wstring::npos) {
                        continue;
                    }
                }

                if (entry.is_directory()) {
                    int64_t entryCount = 0;
                    int64_t entrySize = GetDirectorySize(entryPath, &entryCount, &stopRequested_);
                    if (stopRequested_) return false;
                    if (RemoveTree(entryPath)) {
                        bytesFreed_ += entrySize;
                        filesDeleted_ += entryCount;
                    } else {
                        filesFailed_ += (entryCount > 0 ? entryCount : 1);
                    }
                } else {
                    int64_t entrySize = FileSizeSafe(entryPath);
                    if (DeleteFileWithRetry(entryPath)) {
                        bytesFreed_ += entrySize;
                        filesDeleted_ += 1;
                    } else {
                        filesFailed_ += 1;
                    }
                }
            } catch (...) {
                filesFailed_ += 1;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool Cleaner::CleanupFile(const std::wstring& path, CleanupCategory category) {
    if (!FileExists(path)) return false;
    int64_t size = FileSizeSafe(path);
    if (DeleteFileWithRetry(path)) {
        bytesFreed_ += size;
        filesDeleted_ += 1;
        return true;
    }
    filesFailed_ += 1;
    return false;
}

} // namespace diskcleaner
