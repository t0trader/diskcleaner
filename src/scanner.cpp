#include "scanner.h"
#include "category_paths.h"
#include "utils.h"
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <chrono>

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
    // Copy callbacks under lock to avoid race with SetCallback
    ScanProgressCallback progressCb;
    ScanCompleteCallback completeCb;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        progressCb = progressCallback_;
        completeCb = completeCallback_;
    }

    // 直接遍历全部枚举值，避免维护手写类别列表时遗漏（曾遗漏 QoderCache）
    for (int i = 0; i < static_cast<int>(CleanupCategory::Count); i++) {
        if (stopRequested_) break;
        auto category = static_cast<CleanupCategory>(i);
        if (progressCb) {
            progressCb(category, GetCategoryName(category));
        }
        auto item = ScanCategory(category);
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            auto& slot = results_[static_cast<size_t>(category)];
            // 再次扫描时保留用户手动勾选/取消的选择
            if (slot.scanned) item.enabled = slot.enabled;
            slot = std::move(item);
        }
    }

    // 先落状态再回调，保证 UI 收到完成消息时 IsScanning() 已为 false；
    // StartScan 会 join 旧线程，因此此处提前清标志不会造成并发扫描。
    scanning_ = false;

    bool shouldCallComplete = !stopRequested_ && completeCb;
    if (shouldCallComplete) {
        // 在锁外回调，避免回调方调用 GetResults() 时死锁
        std::vector<CleanupItem> snapshot;
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            snapshot = results_;
        }
        completeCb(snapshot);
    }
}

// Prefetch 目录中最近 24 小时内使用的文件不会被清理，
// 扫描时同样跳过，避免显示的可清理大小高于实际可释放量。
static int64_t GetPrefetchSize(const std::wstring& path, int64_t& fileCount,
                               const std::atomic<bool>& cancel) {
    int64_t total = 0;
    fileCount = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(path,
                fs::directory_options::skip_permission_denied)) {
            if (cancel.load()) break;
            try {
                if (entry.is_symlink() || !entry.is_regular_file()) continue;
                auto age = fs::file_time_type::clock::now() - entry.last_write_time();
                if (age < std::chrono::hours(24)) continue;
                total += entry.file_size();
                ++fileCount;
            } catch (...) {}
        }
    } catch (...) {}
    return total;
}

CleanupItem Scanner::ScanCategory(CleanupCategory category) {
    CleanupItem item;
    item.category = category;
    item.risk = GetCategoryRisk(category);
    item.name = GetCategoryName(category);
    item.description = GetCategoryDescription(category);
    // 默认勾选低/中风险类别；高风险与系统级开关（崩溃转储设置）默认不勾选
    item.enabled = (item.risk != RiskLevel::High) &&
                   category != CleanupCategory::CrashDumpSettings;
    item.scanned = true;

    // Special categories that don't have directory sizes (commands/system-level)
    std::wstring winDir = GetWindowsDir();
    std::wstring sysDrive = GetSystemDrive();
    if (category == CleanupCategory::HibernationFile) {
        // hiberfil.sys size
        std::wstring hiberPath = sysDrive + L"hiberfil.sys";
        if (FileExists(hiberPath)) {
            try {
                item.size = fs::file_size(hiberPath);
                item.path = hiberPath;
                item.fileCount = 1;
            } catch (...) {
                // Cannot access - likely requires admin, estimate ~25% of RAM
                item.size = 0;
            }
        }
        return item;
    }

    if (category == CleanupCategory::MemoryDump) {
        // 同时统计 MEMORY.DMP 文件以及 Minidump / LiveKernelReports 目录
        std::wstring dumpPaths[] = {
            winDir + L"\\MEMORY.DMP",
            winDir + L"\\Minidump",
            winDir + L"\\LiveKernelReports"
        };
        for (const auto& dp : dumpPaths) {
            if (DirectoryExists(dp)) {
                int64_t fc = 0;
                int64_t sz = GetDirectorySize(dp, &fc);
                item.size += sz;
                item.fileCount += fc;
            } else if (FileExists(dp)) {
                try {
                    item.size += fs::file_size(dp);
                    item.fileCount += 1;
                } catch (...) {}
            }
        }
        if (item.size > 0) {
            item.path = winDir + L"\\MEMORY.DMP, Minidump, LiveKernelReports";
        }
        return item;
    }

    if (category == CleanupCategory::EventLogsWEvtUtil) {
        // Event logs - estimate a reasonable size
        item.size = 0; // Will show 0 but still cleanable
        item.path = L"System Event Logs";
        return item;
    }

    if (category == CleanupCategory::PrintSpooler) {
        // Check spooler directory
        std::wstring spoolPath = GetSystemDir() + L"\\spool\\PRINTERS";
        if (DirectoryExists(spoolPath)) {
            int64_t fc = 0;
            item.size = GetDirectorySize(spoolPath, &fc);
            item.fileCount = fc;
            item.path = spoolPath;
        }
        return item;
    }

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
        if (category == CleanupCategory::Prefetch) {
            item.path = path;
            int64_t fileCount = 0;
            item.size += GetPrefetchSize(path, fileCount, stopRequested_);
            item.fileCount += fileCount;
        } else if (DirectoryExists(path)) {
            item.path = path;
            int64_t fileCount = 0;
            int64_t size = GetDirectorySize(path, &fileCount, &stopRequested_);
            item.size += size;
            item.fileCount += fileCount;
        } else if (FileExists(path)) {
            try {
                int64_t sz = fs::file_size(path);
                item.size += sz;
                item.fileCount += 1;
            } catch (...) {}
        }
    }

    return item;
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
        // PowerShell script modules
        case CleanupCategory::ClashVergeLogs:       return L"Clash Verge Logs";
        case CleanupCategory::BrowserUpdateCache:   return L"Browser Update Cache";
        case CleanupCategory::PrintSpooler:         return L"Print Spooler Queue";
        case CleanupCategory::EventTraceLogs:       return L"Event Trace Logs (.etl)";
        case CleanupCategory::RemoteDesktopCache:   return L"Remote Desktop Cache";
        case CleanupCategory::DeliveryOptimization: return L"Delivery Optimization";
        case CleanupCategory::FontCache:            return L"Font Cache";
        case CleanupCategory::NotificationsCache:   return L"Notification Cache";
        case CleanupCategory::QuickAccessHistory:   return L"Quick Access History";
        case CleanupCategory::ClipboardHistory:     return L"Clipboard History";
        case CleanupCategory::PerfLogs:             return L"PerfLogs";
        case CleanupCategory::SetupLogs:            return L"Windows Setup Logs";
        case CleanupCategory::NetSetupLogs:         return L"NetSetup Logs";
        case CleanupCategory::RELogs:               return L"Windows RE Logs";
        case CleanupCategory::DriverStoreTemp:      return L"Driver Store Temp";
        case CleanupCategory::TempFileExtensions:   return L"Temp File Extensions";
        case CleanupCategory::EventLogsWEvtUtil:    return L"Event Logs (wevtutil)";
        case CleanupCategory::IconCache:            return L"Icon Cache";
        case CleanupCategory::RecentDocs:           return L"Recent Documents";
        case CleanupCategory::PowerShellHistory:    return L"PowerShell History";
        case CleanupCategory::INetCache:            return L"Temporary Internet Files";
        case CleanupCategory::ShaderCache:          return L"GPU Shader Cache";
        case CleanupCategory::CrashDumpSettings:    return L"Crash Dump Settings";
        case CleanupCategory::SearchIndex:          return L"Search Index Cache";
        case CleanupCategory::OfficeDefenderCache:  return L"Office & Defender Cache";
        case CleanupCategory::SystemAccountTemp:    return L"System Account Temp";
        case CleanupCategory::Cookies:              return L"Cookies";
        case CleanupCategory::StoreAppCache:        return L"Store App Cache";
        case CleanupCategory::SysWOW64Cache:        return L"32-bit App Cache";
        case CleanupCategory::MemoryDump:           return L"MEMORY.DMP";
        case CleanupCategory::RootLogFiles:         return L"Root Log Files";
        case CleanupCategory::WERFull:              return L"Full WER Reports";
        case CleanupCategory::AspNetTemp:           return L"ASP.NET Temp Files";
        case CleanupCategory::SystemTempDeep:       return L"Deep System Temp";
        case CleanupCategory::FlushDNS:             return L"DNS Cache";
        case CleanupCategory::VSInstallCache:       return L"VS Install Cache";
        case CleanupCategory::VSCodeCache:          return L"VS Code Cache";
        case CleanupCategory::JetBrainsCache:       return L"JetBrains Cache";
        case CleanupCategory::MsBuildCache:         return L"MSBuild Cache";
        case CleanupCategory::NuGetFallback:        return L"NuGet Fallback Packages";
        case CleanupCategory::CBSLogs:              return L"CBS Logs";
        case CleanupCategory::DISMLogs:             return L"DISM Logs";
        case CleanupCategory::IISLogs:              return L"IIS Logs";
        case CleanupCategory::WinSxSBackup:         return L"WinSxS Backup";
        case CleanupCategory::DeviceMetadata:       return L"Device Metadata";
        case CleanupCategory::WindowsUpdateData:    return L"Windows Update Data";
        case CleanupCategory::OneDriveCache:        return L"OneDrive Cache";
        case CleanupCategory::OutlookCache:         return L"Outlook Cache";
        case CleanupCategory::DefenderDefinitionBackup: return L"Defender Definition Backup";
        case CleanupCategory::TerminalCache:        return L"Terminal Cache";
        case CleanupCategory::PowerShellCache:      return L"PowerShell Cache";
        case CleanupCategory::ShellCache:           return L"Shell Cache";
        // AI IDE / Agent tools
        case CleanupCategory::ClaudeCodeCache:      return L"Claude Code Cache";
        case CleanupCategory::CursorCache:          return L"Cursor Cache";
        case CleanupCategory::WindsurfCache:        return L"Windsurf Cache";
        case CleanupCategory::TraeCache:            return L"Trae Cache";
        case CleanupCategory::QoderCache:           return L"Qoder Cache";
        case CleanupCategory::ComateCache:          return L"Comate Cache";
        case CleanupCategory::CopilotChatCache:     return L"Copilot Chat Cache";
        case CleanupCategory::JetBrainsAICache:     return L"JetBrains AI Cache";
        case CleanupCategory::TabnineCache:         return L"Tabnine Cache";
        case CleanupCategory::ContinueCache:        return L"Continue Cache";
        case CleanupCategory::ClineCache:           return L"Cline Cache";
        case CleanupCategory::RooCache:             return L"Roo Code Cache";
        case CleanupCategory::AiderCache:           return L"Aider Cache";
        case CleanupCategory::CodexCache:           return L"Codex CLI Cache";
        case CleanupCategory::GeminiCache:          return L"Gemini CLI Cache";
        case CleanupCategory::ZedCache:             return L"Zed Cache";
        case CleanupCategory::AugmentCache:         return L"Augment Cache";
        case CleanupCategory::LingmaCache:          return L"Lingma Cache";
        case CleanupCategory::CodeGeeXCache:        return L"CodeGeeX Cache";
        case CleanupCategory::WarpCache:            return L"Warp Terminal Cache";
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
        // PowerShell script modules
        case CleanupCategory::ClashVergeLogs:
            return L"Clash Verge proxy application logs";
        case CleanupCategory::BrowserUpdateCache:
            return L"Google Chrome and Microsoft Edge update download cache";
        case CleanupCategory::PrintSpooler:
            return L"Pending print job files in spooler queue";
        case CleanupCategory::EventTraceLogs:
            return L"WMI event trace log files (.etl)";
        case CleanupCategory::RemoteDesktopCache:
            return L"Remote Desktop Connection saved server cache";
        case CleanupCategory::DeliveryOptimization:
            return L"Windows Delivery Optimization cache and logs";
        case CleanupCategory::FontCache:
            return L"Windows font cache service data, auto-recreated";
        case CleanupCategory::NotificationsCache:
            return L"Windows notification and push message cache databases";
        case CleanupCategory::QuickAccessHistory:
            return L"File Explorer Quick Access pinned and recent history";
        case CleanupCategory::ClipboardHistory:
            return L"Windows clipboard history data";
        case CleanupCategory::PerfLogs:
            return L"System performance monitoring log directory";
        case CleanupCategory::SetupLogs:
            return L"Windows Setup and Setup Cleanup log files";
        case CleanupCategory::NetSetupLogs:
            return L"Network setup log files";
        case CleanupCategory::RELogs:
            return L"Windows Recovery Environment log files";
        case CleanupCategory::DriverStoreTemp:
            return L"Driver Store temporary extraction files";
        case CleanupCategory::TempFileExtensions:
            return L"Root-level temporary files with .tmp/.temp extensions";
        case CleanupCategory::EventLogsWEvtUtil:
            return L"All Windows event logs (Application, System, Security)";
        case CleanupCategory::IconCache:
            return L"File explorer icon cache database, auto-recreated";
        case CleanupCategory::RecentDocs:
            return L"Recent documents shortcut files in Start menu";
        case CleanupCategory::PowerShellHistory:
            return L"PowerShell command history (PSReadLine)";
        case CleanupCategory::INetCache:
            return L"Internet Explorer/Edge legacy temporary files";
        case CleanupCategory::ShaderCache:
            return L"GPU shader compilation cache (NVIDIA/AMD/Intel), auto-recreated";
        case CleanupCategory::SearchIndex:
            return L"Windows Search index database files";
        case CleanupCategory::OfficeDefenderCache:
            return L"Microsoft Office lock files and Windows Defender scan history";
        case CleanupCategory::SystemAccountTemp:
            return L"Temp files for the SYSTEM account profile";
        case CleanupCategory::Cookies:
            return L"User cookies from browser and system";
        case CleanupCategory::StoreAppCache:
            return L"Microsoft Store app local cache and temp state";
        case CleanupCategory::SysWOW64Cache:
            return L"32-bit application temp files under SysWOW64";
        case CleanupCategory::MemoryDump:
            return L"Full system memory dump file";
        case CleanupCategory::RootLogFiles:
            return L"Log files directly in C:\\ and C:\\Windows\\";
        case CleanupCategory::WERFull:
            return L"Complete Windows Error Reporting data (archives, queues, temp)";
        case CleanupCategory::AspNetTemp:
            return L"Compiled ASP.NET temporary files, auto-recreated";
        case CleanupCategory::SystemTempDeep:
            return L"Service account temp files (LocalService, NetworkService)";
        case CleanupCategory::FlushDNS:
            return L"DNS resolver cache, auto-recreated after flush";
        case CleanupCategory::VSInstallCache:
            return L"Visual Studio installation package cache and setup data";
        case CleanupCategory::VSCodeCache:
            return L"VS Code cache, logs and workspace storage";
        case CleanupCategory::JetBrainsCache:
            return L"JetBrains IDE (Rider/CLion/etc) cache files";
        case CleanupCategory::MsBuildCache:
            return L"MSBuild build cache files";
        case CleanupCategory::NuGetFallback:
            return L"NuGet fallback package folder";
        case CleanupCategory::CBSLogs:
            return L"Windows Component-Based Servicing logs";
        case CleanupCategory::DISMLogs:
            return L"Deployment Image Servicing and Management logs";
        case CleanupCategory::IISLogs:
            return L"IIS web server access logs";
        case CleanupCategory::WinSxSBackup:
            return L"Windows Side-by-Side component backup files";
        case CleanupCategory::DeviceMetadata:
            return L"Windows device metadata store";
        case CleanupCategory::WindowsUpdateData:
            return L"Windows Update service data files";
        case CleanupCategory::OneDriveCache:
            return L"OneDrive cache and logs (account settings preserved)";
        case CleanupCategory::OutlookCache:
            return L"Outlook cache files (OST data and accounts preserved)";
        case CleanupCategory::DefenderDefinitionBackup:
            return L"Windows Defender definition update backups";
        case CleanupCategory::TerminalCache:
            return L"Windows Terminal local cache";
        case CleanupCategory::PowerShellCache:
            return L"PowerShell module and script cache";
        case CleanupCategory::ShellCache:
            return L"Windows Shell layout and state cache";
        case CleanupCategory::CrashDumpSettings:
            return L"Disable crash dump generation via registry (reversible)";
        // AI IDE / Agent tools
        case CleanupCategory::ClaudeCodeCache:
            return L"Claude Code cache, paste cache, telemetry, project history and command history";
        case CleanupCategory::CursorCache:
            return L"Cursor editor cache, logs, workspace storage and extension cache";
        case CleanupCategory::WindsurfCache:
            return L"Windsurf editor cache, logs, workspace storage and extension cache";
        case CleanupCategory::TraeCache:
            return L"Trae editor cache, logs, workspace storage and extension cache";
        case CleanupCategory::QoderCache:
            return L"Qoder (阿里巴巴) editor cache, logs and workspace storage";
        case CleanupCategory::ComateCache:
            return L"Baidu Comate editor cache, logs and workspace storage";
        case CleanupCategory::CopilotChatCache:
            return L"GitHub Copilot Chat extension cache";
        case CleanupCategory::JetBrainsAICache:
            return L"JetBrains AI Assistant cache (IDE settings preserved)";
        case CleanupCategory::TabnineCache:
            return L"Tabnine local model and cache files";
        case CleanupCategory::ContinueCache:
            return L"Continue extension cache and temp files";
        case CleanupCategory::ClineCache:
            return L"Cline extension cache files";
        case CleanupCategory::RooCache:
            return L"Roo Code extension cache files";
        case CleanupCategory::AiderCache:
            return L"Aider assistant cache files";
        case CleanupCategory::CodexCache:
            return L"Codex CLI logs, temp and history (auth/config preserved)";
        case CleanupCategory::GeminiCache:
            return L"Gemini CLI temp and cache (settings/credentials preserved)";
        case CleanupCategory::ZedCache:
            return L"Zed editor cache, temp and logs (settings preserved)";
        case CleanupCategory::AugmentCache:
            return L"Augment Code extension cache files";
        case CleanupCategory::LingmaCache:
            return L"Tongyi Lingma (通义灵码) cache files";
        case CleanupCategory::CodeGeeXCache:
            return L"CodeGeeX extension cache files";
        case CleanupCategory::WarpCache:
            return L"Warp terminal cache, temp and logs (settings preserved)";
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
            return RiskLevel::Medium;

        // High risk - may affect system functionality or user data
        case CleanupCategory::Prefetch:
        case CleanupCategory::WindowsUpdate:
        case CleanupCategory::RecycleBin:
        case CleanupCategory::WindowsOld:
        case CleanupCategory::HibernationFile:
        case CleanupCategory::RecoveryFiles:
        case CleanupCategory::InstallerCache:
        case CleanupCategory::WeChatCache:      // 涉及聊天记录/接收文件
        case CleanupCategory::QQCache:          // 涉及聊天记录/接收文件
            return RiskLevel::High;

        // New categories - mostly low/medium risk
        case CleanupCategory::EventLogsWEvtUtil:
        case CleanupCategory::SearchIndex:
            return RiskLevel::High;  // clearing all event logs / search index is disruptive

        case CleanupCategory::PrintSpooler:
        case CleanupCategory::EventTraceLogs:
        case CleanupCategory::DeliveryOptimization:
        case CleanupCategory::FontCache:
        case CleanupCategory::NotificationsCache:
        case CleanupCategory::QuickAccessHistory:
        case CleanupCategory::ClipboardHistory:
        case CleanupCategory::PerfLogs:
        case CleanupCategory::SetupLogs:
        case CleanupCategory::NetSetupLogs:
        case CleanupCategory::RELogs:
        case CleanupCategory::DriverStoreTemp:
        case CleanupCategory::TempFileExtensions:
        case CleanupCategory::IconCache:
        case CleanupCategory::RecentDocs:
        case CleanupCategory::INetCache:
        case CleanupCategory::SystemAccountTemp:
        case CleanupCategory::Cookies:
        case CleanupCategory::StoreAppCache:
        case CleanupCategory::SysWOW64Cache:
        case CleanupCategory::MemoryDump:
        case CleanupCategory::RootLogFiles:
        case CleanupCategory::WERFull:
        case CleanupCategory::AspNetTemp:
        case CleanupCategory::SystemTempDeep:
        case CleanupCategory::ClashVergeLogs:
        case CleanupCategory::BrowserUpdateCache:
        case CleanupCategory::RemoteDesktopCache:
            return RiskLevel::Low;

        case CleanupCategory::ShaderCache:
        case CleanupCategory::PowerShellHistory:
        case CleanupCategory::OfficeDefenderCache:
        case CleanupCategory::FlushDNS:
        case CleanupCategory::VSInstallCache:
        case CleanupCategory::VSCodeCache:
        case CleanupCategory::JetBrainsCache:
        case CleanupCategory::MsBuildCache:
        case CleanupCategory::NuGetFallback:
        case CleanupCategory::DefenderDefinitionBackup:
        case CleanupCategory::TerminalCache:
        case CleanupCategory::PowerShellCache:
        case CleanupCategory::ShellCache:
        case CleanupCategory::CrashDumpSettings:
            return RiskLevel::Medium;

        // High risk - OneDrive/Outlook 涉及账户与邮件数据
        case CleanupCategory::OneDriveCache:
        case CleanupCategory::OutlookCache:
            return RiskLevel::High;

        case CleanupCategory::CBSLogs:
        case CleanupCategory::DISMLogs:
        case CleanupCategory::IISLogs:
        case CleanupCategory::WinSxSBackup:
        case CleanupCategory::DeviceMetadata:
        case CleanupCategory::WindowsUpdateData:
            return RiskLevel::High;

        // AI IDE / Agent tools - 缓存可重建，但部分含会话/历史数据
        case CleanupCategory::CursorCache:
        case CleanupCategory::WindsurfCache:
        case CleanupCategory::TraeCache:
        case CleanupCategory::QoderCache:
        case CleanupCategory::ComateCache:
        case CleanupCategory::CopilotChatCache:
        case CleanupCategory::JetBrainsAICache:
        case CleanupCategory::TabnineCache:
            return RiskLevel::Medium;

        // 会删除会话记录/命令历史，默认不勾选
        case CleanupCategory::ClaudeCodeCache:
        case CleanupCategory::CodexCache:
            return RiskLevel::High;

        case CleanupCategory::ContinueCache:
        case CleanupCategory::ClineCache:
        case CleanupCategory::RooCache:
        case CleanupCategory::AiderCache:
        case CleanupCategory::GeminiCache:
        case CleanupCategory::ZedCache:
        case CleanupCategory::AugmentCache:
        case CleanupCategory::LingmaCache:
        case CleanupCategory::CodeGeeXCache:
        case CleanupCategory::WarpCache:
            return RiskLevel::Low;

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