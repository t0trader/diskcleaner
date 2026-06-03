#include "cleaner.h"
#include "utils.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace diskcleaner {

static std::vector<std::wstring> GetCleanupPaths(CleanupCategory category) {
    std::vector<std::wstring> paths;
    switch (category) {
        case CleanupCategory::UserTemp:
            paths.push_back(ExpandPath(L"%TEMP%"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Temp"));
            break;
        case CleanupCategory::SystemTemp:
            paths.push_back(L"C:\\Windows\\Temp");
            break;
        case CleanupCategory::Prefetch:
            paths.push_back(L"C:\\Windows\\Prefetch");
            break;
        case CleanupCategory::WindowsUpdate:
            paths.push_back(L"C:\\Windows\\SoftwareDistribution\\Download");
            break;
        case CleanupCategory::ThumbnailCache:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer"));
            break;
        case CleanupCategory::ChromeCache:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Cache"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Code Cache"));
            break;
        case CleanupCategory::FirefoxCache:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles"));
            break;
        case CleanupCategory::EdgeCache:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Cache"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Code Cache"));
            break;
        case CleanupCategory::ErrorReports:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\Windows\\WER"));
            paths.push_back(L"C:\\ProgramData\\Microsoft\\Windows\\WER");
            break;
        case CleanupCategory::CrashDumps:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\CrashDumps"));
            paths.push_back(L"C:\\Windows\\Minidump");
            break;
        case CleanupCategory::WindowsOld:
            paths.push_back(L"C:\\Windows.old");
            break;
        case CleanupCategory::HibernationFile:
            // Skip - requires powercfg command, not file deletion
            break;
        case CleanupCategory::RecoveryFiles:
            // Skip - system protected, cannot delete directly
            break;
        case CleanupCategory::VisualStudioCache: {
            // Enumerate version subdirectories (e.g. 18.0_xxxxx) and only clean cache subdirs
            std::wstring vsRoot = ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\VisualStudio");
            if (DirectoryExists(vsRoot)) {
                try {
                    for (const auto& entry : fs::directory_iterator(vsRoot,
                            fs::directory_options::skip_permission_denied)) {
                        if (!entry.is_directory()) continue;
                        std::wstring dirName = entry.path().filename().wstring();
                        // Version dirs match pattern like "18.0_xxxxxxxxxxxxx"
                        if (dirName.find(L'.') != std::wstring::npos &&
                            dirName.find(L'_') != std::wstring::npos) {
                            // Only clean known-safe cache subdirectories, preserve settings
                            paths.push_back(entry.path().wstring() + L"\\ComponentModelCache");
                            paths.push_back(entry.path().wstring() + L"\\Roslyn");
                            paths.push_back(entry.path().wstring() + L"\\ActivityLog.xml");
                        }
                    }
                } catch (...) {}
            }
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\VSApplicationInsights"));
            paths.push_back(ExpandPath(L"%PROGRAMDATA%\\Microsoft\\VisualStudio\\Packages"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\NuGet\\Cache"));
            paths.push_back(ExpandPath(L"%TEMP%\\VisualStudio"));
            break;
        }
        case CleanupCategory::NuGetCache:
            paths.push_back(ExpandPath(L"%USERPROFILE%\\.nuget\\packages"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\NuGet\\Cache"));
            break;
        case CleanupCategory::NpmCache:
            paths.push_back(ExpandPath(L"%APPDATA%\\npm-cache"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\npm-cache"));
            break;
        case CleanupCategory::PipCache:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\pip\\cache"));
            paths.push_back(ExpandPath(L"%USERPROFILE%\\AppData\\Local\\pip\\cache"));
            break;
        case CleanupCategory::TeamsCache:
            paths.push_back(ExpandPath(L"%APPDATA%\\Microsoft\\Teams\\Cache"));
            paths.push_back(ExpandPath(L"%APPDATA%\\Microsoft\\Teams\\logs"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\Teams\\Cache"));
            break;
        case CleanupCategory::ZoomCache:
            paths.push_back(ExpandPath(L"%APPDATA%\\Zoom\\bin\\logs"));
            paths.push_back(ExpandPath(L"%USERPROFILE%\\AppData\\Roaming\\Zoom\\logs"));
            break;
        case CleanupCategory::WeChatCache:
            paths.push_back(ExpandPath(L"%USERPROFILE%\\Documents\\WeChat Files"));
            paths.push_back(ExpandPath(L"%APPDATA%\\Tencent\\WeChat\\Cache"));
            break;
        case CleanupCategory::QQCache:
            paths.push_back(ExpandPath(L"%USERPROFILE%\\Documents\\Tencent Files"));
            paths.push_back(ExpandPath(L"%APPDATA%\\Tencent\\QQ\\Cache"));
            break;
        case CleanupCategory::InstallerCache:
            paths.push_back(L"C:\\Windows\\Installer\\$PatchCache$");
            break;
        default:
            break;
    }
    return paths;
}

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

    std::vector<CleanupItem> enabledItems;
    for (const auto& item : items) {
        if (item.enabled && item.size > 0) {
            enabledItems.push_back(item);
        }
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

    bool shouldCallComplete = !stopRequested_ && completeCb;
    if (shouldCallComplete) {
        completeCb(bytesFreed_.load(), filesDeleted_.load());
    }

    cleaning_ = false;
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

    auto paths = GetCleanupPaths(item.category);
    for (const auto& path : paths) {
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
    try {
        for (const auto& entry : fs::directory_iterator(path,
                fs::directory_options::skip_permission_denied)) {
            if (stopRequested_) return false;
            try {
                std::wstring entryPath = entry.path().wstring();
                if (progressCb) {
                    progressCb(category, entryPath, bytesFreed_.load());
                }

                if (category == CleanupCategory::Prefetch) {
                    auto lastWrite = entry.last_write_time();
                    auto now = fs::file_time_type::clock::now();
                    auto age = now - lastWrite;
                    if (age < std::chrono::hours(24)) continue;
                }

                if (category == CleanupCategory::ThumbnailCache) {
                    std::wstring filename = entry.path().filename().wstring();
                    if (filename.find(L"thumbcache") == std::wstring::npos &&
                        filename.find(L"iconcache") == std::wstring::npos) {
                        continue;
                    }
                }

                int64_t entrySize = 0;
                int64_t entryCount = 0;

                if (entry.is_directory()) {
                    entrySize = GetDirectorySize(entryPath, &entryCount);
                    fs::remove_all(entryPath);
                } else {
                    entrySize = entry.file_size();
                    entryCount = 1;
                    fs::remove(entryPath);
                }

                bytesFreed_ += entrySize;
                filesDeleted_ += entryCount;
            } catch (...) {}
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool Cleaner::CleanupFile(const std::wstring& path, CleanupCategory category) {
    try {
        if (!fs::exists(path)) return false;
        int64_t size = fs::file_size(path);
        if (fs::remove(path)) {
            bytesFreed_ += size;
            filesDeleted_ += 1;
            return true;
        }
    } catch (...) {}
    return false;
}

} // namespace diskcleaner