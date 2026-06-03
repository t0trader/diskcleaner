#include "scanner.h"
#include "utils.h"
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace diskcleaner {

Scanner::Scanner() {
    results_.resize(static_cast<size_t>(CleanupCategory::Count));
    for (size_t i = 0; i < results_.size(); i++) {
        results_[i].category = static_cast<CleanupCategory>(i);
        results_[i].name = GetCategoryName(static_cast<CleanupCategory>(i));
        results_[i].description = GetCategoryDescription(static_cast<CleanupCategory>(i));
    }
}

Scanner::~Scanner() {
    StopScan();
}

void Scanner::StartScan() {
    // If already scanning, stop first
    if (scanning_.load()) {
        StopScan();
    }

    // Wait for previous thread to finish
    if (scanThread_.joinable()) {
        scanThread_.join();
    }

    stopRequested_ = false;
    scanning_ = true;
    scanThread_ = std::thread(&Scanner::ScanWorker, this);
}

void Scanner::StopScan() {
    stopRequested_ = true;
    scanning_ = false;
    if (scanThread_.joinable()) {
        scanThread_.join();
    }
}

std::vector<CleanupItem> Scanner::GetResults() const {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    return results_;
}

int64_t Scanner::GetTotalSize() const {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    int64_t total = 0;
    for (const auto& item : results_) {
        if (item.enabled) total += item.size;
    }
    return total;
}

int64_t Scanner::GetTotalFileCount() const {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    int64_t total = 0;
    for (const auto& item : results_) {
        if (item.enabled) total += item.fileCount;
    }
    return total;
}

void Scanner::SetItemEnabled(CleanupCategory category, bool enabled) {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    auto idx = static_cast<size_t>(category);
    if (idx < results_.size()) {
        results_[idx].enabled = enabled;
    }
}

void Scanner::ScanWorker() {
    CleanupCategory categories[] = {
        CleanupCategory::UserTemp,
        CleanupCategory::SystemTemp,
        CleanupCategory::Prefetch,
        CleanupCategory::WindowsUpdate,
        CleanupCategory::ThumbnailCache,
        CleanupCategory::ChromeCache,
        CleanupCategory::FirefoxCache,
        CleanupCategory::EdgeCache,
        CleanupCategory::ErrorReports,
        CleanupCategory::CrashDumps,
        CleanupCategory::RecycleBin,
        CleanupCategory::WindowsOld,
        CleanupCategory::HibernationFile,
        CleanupCategory::RecoveryFiles,
        CleanupCategory::VisualStudioCache,
        CleanupCategory::NuGetCache,
        CleanupCategory::NpmCache,
        CleanupCategory::PipCache,
        CleanupCategory::TeamsCache,
        CleanupCategory::ZoomCache,
        CleanupCategory::WeChatCache,
        CleanupCategory::QQCache,
        CleanupCategory::InstallerCache,
    };

    // Copy callbacks under lock to avoid race with SetCallback
    ScanProgressCallback progressCb;
    ScanCompleteCallback completeCb;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        progressCb = progressCallback_;
        completeCb = completeCallback_;
    }

    for (auto category : categories) {
        if (stopRequested_) break;
        if (progressCb) {
            progressCb(category, GetCategoryName(category));
        }
        auto item = ScanCategory(category);
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            results_[static_cast<size_t>(category)] = std::move(item);
        }
    }

    bool shouldCallComplete = !stopRequested_ && completeCb;
    if (shouldCallComplete) {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        completeCb(results_);
    }

    scanning_ = false;
}

CleanupItem Scanner::ScanCategory(CleanupCategory category) {
    CleanupItem item;
    item.category = category;
    item.risk = GetCategoryRisk(category);
    item.name = GetCategoryName(category);
    item.description = GetCategoryDescription(category);
    // Default: enable low/medium risk, disable high risk
    item.enabled = (item.risk != RiskLevel::High);
    item.scanned = true;

    if (category == CleanupCategory::RecycleBin) {
        item.path = L"$Recycle.Bin";
        try {
            SHQUERYRBINFO rbInfo = {};
            rbInfo.cbSize = sizeof(SHQUERYRBINFO);
            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &rbInfo))) {
                item.size = rbInfo.i64Size;
                item.fileCount = rbInfo.i64NumItems;
            }
        } catch (...) {}
        return item;
    }

    auto paths = GetCategoryPaths(category);
    for (const auto& path : paths) {
        if (stopRequested_) break;
        if (DirectoryExists(path)) {
            item.path = path;
            int64_t fileCount = 0;
            int64_t size = GetDirectorySize(path, &fileCount);
            item.size += size;
            item.fileCount += fileCount;
        }
    }

    return item;
}

std::vector<std::wstring> Scanner::GetCategoryPaths(CleanupCategory category) {
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
        // Hibernation file (single file, needs special handling)
        case CleanupCategory::HibernationFile:
            paths.push_back(L"C:\\hiberfil.sys");
            break;
        // Recovery files
        case CleanupCategory::RecoveryFiles:
            paths.push_back(L"C:\\Recovery");
            paths.push_back(ExpandPath(L"%SYSTEMDRIVE%\\$RECYCLE.BIN"));
            break;
        // Visual Studio caches
        case CleanupCategory::VisualStudioCache:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\VisualStudio"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\VSApplicationInsights"));
            paths.push_back(ExpandPath(L"%PROGRAMDATA%\\Microsoft\\VisualStudio\\Packages"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\NuGet\\Cache"));
            paths.push_back(ExpandPath(L"%TEMP%\\VisualStudio"));
            break;
        // NuGet package cache
        case CleanupCategory::NuGetCache:
            paths.push_back(ExpandPath(L"%USERPROFILE%\\.nuget\\packages"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\NuGet\\Cache"));
            break;
        // npm cache
        case CleanupCategory::NpmCache:
            paths.push_back(ExpandPath(L"%APPDATA%\\npm-cache"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\npm-cache"));
            break;
        // pip cache
        case CleanupCategory::PipCache:
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\pip\\cache"));
            paths.push_back(ExpandPath(L"%USERPROFILE%\\AppData\\Local\\pip\\cache"));
            break;
        // Microsoft Teams cache
        case CleanupCategory::TeamsCache:
            paths.push_back(ExpandPath(L"%APPDATA%\\Microsoft\\Teams\\Cache"));
            paths.push_back(ExpandPath(L"%APPDATA%\\Microsoft\\Teams\\logs"));
            paths.push_back(ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\Teams\\Cache"));
            break;
        // Zoom cache
        case CleanupCategory::ZoomCache:
            paths.push_back(ExpandPath(L"%APPDATA%\\Zoom\\bin\\logs"));
            paths.push_back(ExpandPath(L"%USERPROFILE%\\AppData\\Roaming\\Zoom\\logs"));
            break;
        // WeChat cache
        case CleanupCategory::WeChatCache:
            paths.push_back(ExpandPath(L"%USERPROFILE%\\Documents\\WeChat Files"));
            paths.push_back(ExpandPath(L"%APPDATA%\\Tencent\\WeChat\\Cache"));
            break;
        // QQ cache
        case CleanupCategory::QQCache:
            paths.push_back(ExpandPath(L"%USERPROFILE%\\Documents\\Tencent Files"));
            paths.push_back(ExpandPath(L"%APPDATA%\\Tencent\\QQ\\Cache"));
            break;
        // Windows Installer cache
        case CleanupCategory::InstallerCache:
            paths.push_back(L"C:\\Windows\\Installer\\$PatchCache$");
            paths.push_back(L"C:\\Windows\\Installer\\SourceHash*");
            break;
        default:
            break;
    }
    return paths;
}

std::wstring Scanner::GetCategoryName(CleanupCategory category) {
    switch (category) {
        case CleanupCategory::UserTemp:             return L"User Temp Files";
        case CleanupCategory::SystemTemp:           return L"System Temp Files";
        case CleanupCategory::Prefetch:             return L"Prefetch Files";
        case CleanupCategory::WindowsUpdate:        return L"Windows Update Cache";
        case CleanupCategory::ThumbnailCache:       return L"Thumbnail Cache";
        case CleanupCategory::ChromeCache:          return L"Chrome Cache";
        case CleanupCategory::FirefoxCache:         return L"Firefox Cache";
        case CleanupCategory::EdgeCache:            return L"Edge Cache";
        case CleanupCategory::ErrorReports:         return L"Error Reports";
        case CleanupCategory::CrashDumps:           return L"Crash Dumps";
        case CleanupCategory::RecycleBin:           return L"Recycle Bin";
        case CleanupCategory::WindowsOld:           return L"Previous Windows Install";
        case CleanupCategory::HibernationFile:      return L"Hibernation File";
        case CleanupCategory::RecoveryFiles:        return L"Recovery Files";
        case CleanupCategory::VisualStudioCache:    return L"Visual Studio Cache";
        case CleanupCategory::NuGetCache:           return L"NuGet Package Cache";
        case CleanupCategory::NpmCache:             return L"npm Cache";
        case CleanupCategory::PipCache:             return L"pip Cache";
        case CleanupCategory::TeamsCache:           return L"Microsoft Teams Cache";
        case CleanupCategory::ZoomCache:            return L"Zoom Cache";
        case CleanupCategory::WeChatCache:          return L"WeChat Cache";
        case CleanupCategory::QQCache:              return L"QQ Cache";
        case CleanupCategory::InstallerCache:       return L"Windows Installer Cache";
        default:                                    return L"Unknown";
    }
}

std::wstring Scanner::GetCategoryDescription(CleanupCategory category) {
    switch (category) {
        case CleanupCategory::UserTemp:
            return L"Temp files created by applications, safe to delete";
        case CleanupCategory::SystemTemp:
            return L"System temp files, safe to delete";
        case CleanupCategory::Prefetch:
            return L"Application prefetch cache, auto-recreated after deletion";
        case CleanupCategory::WindowsUpdate:
            return L"Downloaded Windows update packages";
        case CleanupCategory::ThumbnailCache:
            return L"Explorer thumbnail cache, auto-recreated";
        case CleanupCategory::ChromeCache:
            return L"Google Chrome browser cache";
        case CleanupCategory::FirefoxCache:
            return L"Mozilla Firefox browser cache";
        case CleanupCategory::EdgeCache:
            return L"Microsoft Edge browser cache";
        case CleanupCategory::ErrorReports:
            return L"Windows error reports and diagnostics";
        case CleanupCategory::CrashDumps:
            return L"Application crash dump files";
        case CleanupCategory::RecycleBin:
            return L"Deleted files not yet permanently removed";
        case CleanupCategory::WindowsOld:
            return L"Previous Windows installation files";
        case CleanupCategory::HibernationFile:
            return L"Disable hibernation to free disk space (run powercfg -h off)";
        case CleanupCategory::RecoveryFiles:
            return L"System recovery files, cannot be restored once deleted";
        case CleanupCategory::VisualStudioCache:
            return L"VS component cache, NuGet cache, temp files";
        case CleanupCategory::NuGetCache:
            return L"Downloaded NuGet packages (.nuget\\packages)";
        case CleanupCategory::NpmCache:
            return L"Downloaded npm packages cache";
        case CleanupCategory::PipCache:
            return L"Downloaded pip packages cache";
        case CleanupCategory::TeamsCache:
            return L"Microsoft Teams cache and logs";
        case CleanupCategory::ZoomCache:
            return L"Zoom application logs";
        case CleanupCategory::WeChatCache:
            return L"WeChat chat cache and files (check before delete)";
        case CleanupCategory::QQCache:
            return L"QQ chat cache and files (check before delete)";
        case CleanupCategory::InstallerCache:
            return L"Windows Installer patch cache";
        default:
            return L"";
    }
}

RiskLevel Scanner::GetCategoryRisk(CleanupCategory category) {
    switch (category) {
        // Low risk - safe to delete
        case CleanupCategory::UserTemp:
        case CleanupCategory::SystemTemp:
        case CleanupCategory::ThumbnailCache:
        case CleanupCategory::ChromeCache:
        case CleanupCategory::FirefoxCache:
        case CleanupCategory::EdgeCache:
        case CleanupCategory::ErrorReports:
        case CleanupCategory::CrashDumps:
        case CleanupCategory::TeamsCache:
        case CleanupCategory::ZoomCache:
            return RiskLevel::Low;

        // Medium risk - may need re-download/rebuild
        case CleanupCategory::VisualStudioCache:
        case CleanupCategory::NuGetCache:
        case CleanupCategory::NpmCache:
        case CleanupCategory::PipCache:
        case CleanupCategory::WeChatCache:
        case CleanupCategory::QQCache:
            return RiskLevel::Medium;

        // High risk - may affect system functionality
        case CleanupCategory::Prefetch:
        case CleanupCategory::WindowsUpdate:
        case CleanupCategory::RecycleBin:
        case CleanupCategory::WindowsOld:
        case CleanupCategory::HibernationFile:
        case CleanupCategory::RecoveryFiles:
        case CleanupCategory::InstallerCache:
            return RiskLevel::High;

        default:
            return RiskLevel::Low;
    }
}

std::wstring Scanner::GetRiskLevelName(RiskLevel level) {
    switch (level) {
        case RiskLevel::Low:    return L"Low";
        case RiskLevel::Medium: return L"Medium";
        case RiskLevel::High:   return L"High";
        default:                return L"Unknown";
    }
}

} // namespace diskcleaner