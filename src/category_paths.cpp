#include "category_paths.h"
#include "utils.h"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace diskcleaner {

namespace {

// 展开通配符/环境变量后追加路径，并按大小写不敏感去重
// （%TEMP% 与 %LOCALAPPDATA%\Temp 常常指向同一目录，重复统计会导致大小翻倍）。
void AddPath(std::vector<std::wstring>& paths, const std::wstring& pattern) {
    for (auto& expanded : ExpandGlob(pattern)) {
        bool duplicate = false;
        for (const auto& existing : paths) {
            if (_wcsicmp(existing.c_str(), expanded.c_str()) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            paths.push_back(std::move(expanded));
        }
    }
}

} // namespace

std::vector<std::wstring> GetCategoryPaths(CleanupCategory category) {
    std::vector<std::wstring> paths;
    const std::wstring winDir = GetWindowsDir();
    const std::wstring sysDir = GetSystemDir();
    const std::wstring sysDrive = GetSystemDrive();

    switch (category) {
        case CleanupCategory::UserTemp:
            AddPath(paths, L"%TEMP%");
            AddPath(paths, L"%LOCALAPPDATA%\\Temp");
            break;
        case CleanupCategory::SystemTemp:
            AddPath(paths, winDir + L"\\Temp");
            break;
        case CleanupCategory::Prefetch:
            AddPath(paths, winDir + L"\\Prefetch");
            break;
        case CleanupCategory::WindowsUpdate:
            AddPath(paths, winDir + L"\\SoftwareDistribution\\Download");
            break;
        case CleanupCategory::ThumbnailCache:
            // 只匹配缩略图缓存文件（图标缓存归 IconCache 分类）
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\thumbcache_*");
            break;
        case CleanupCategory::ChromeCache:
            // 匹配所有 Chrome 用户配置（Default、Profile 1 ...）
            AddPath(paths, L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Cache");
            AddPath(paths, L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Code Cache");
            AddPath(paths, L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\GPUCache");
            break;
        case CleanupCategory::FirefoxCache:
            // 只清理缓存目录，保留书签/密码/配置
            AddPath(paths, L"%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles\\*\\cache2");
            AddPath(paths, L"%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles\\*\\startupCache");
            AddPath(paths, L"%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles\\*\\thumbnails");
            break;
        case CleanupCategory::EdgeCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Cache");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Code Cache");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\GPUCache");
            break;
        case CleanupCategory::ErrorReports:
            // 用户级错误报告（系统级 WER 数据归 WERFull 分类）
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\WER");
            break;
        case CleanupCategory::CrashDumps:
            // 应用崩溃转储（系统 Minidump 归 MemoryDump 分类）
            AddPath(paths, L"%LOCALAPPDATA%\\CrashDumps");
            break;
        case CleanupCategory::WindowsOld:
            AddPath(paths, sysDrive + L"Windows.old");
            break;
        case CleanupCategory::HibernationFile:
            // 实际通过 powercfg -h off 关闭，此处仅用于存在性检查
            AddPath(paths, sysDrive + L"hiberfil.sys");
            break;
        case CleanupCategory::RecoveryFiles:
            AddPath(paths, sysDrive + L"Recovery");
            break;
        case CleanupCategory::VisualStudioCache: {
            // 只枚举版本子目录下的缓存，保留用户设置
            std::wstring vsRoot = ExpandPath(L"%LOCALAPPDATA%\\Microsoft\\VisualStudio");
            if (DirectoryExists(vsRoot)) {
                try {
                    for (const auto& entry : fs::directory_iterator(vsRoot,
                            fs::directory_options::skip_permission_denied)) {
                        if (!entry.is_directory()) continue;
                        std::wstring dirName = entry.path().filename().wstring();
                        // 版本目录形如 "18.0_xxxxxxxxxxxxx"
                        if (dirName.find(L'.') != std::wstring::npos &&
                            dirName.find(L'_') != std::wstring::npos) {
                            AddPath(paths, entry.path().wstring() + L"\\ComponentModelCache");
                            AddPath(paths, entry.path().wstring() + L"\\Roslyn");
                            AddPath(paths, entry.path().wstring() + L"\\ActivityLog.xml");
                        }
                    }
                } catch (...) {}
            }
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\VSApplicationInsights");
            AddPath(paths, L"%TEMP%\\VisualStudio");
            break;
        }
        case CleanupCategory::NuGetCache:
            AddPath(paths, L"%USERPROFILE%\\.nuget\\packages");
            AddPath(paths, L"%LOCALAPPDATA%\\NuGet\\Cache");
            break;
        case CleanupCategory::NpmCache:
            AddPath(paths, L"%APPDATA%\\npm-cache");
            AddPath(paths, L"%LOCALAPPDATA%\\npm-cache");
            break;
        case CleanupCategory::PipCache:
            AddPath(paths, L"%LOCALAPPDATA%\\pip\\cache");
            AddPath(paths, L"%USERPROFILE%\\AppData\\Local\\pip\\cache");
            break;
        case CleanupCategory::TeamsCache:
            AddPath(paths, L"%APPDATA%\\Microsoft\\Teams\\Cache");
            AddPath(paths, L"%APPDATA%\\Microsoft\\Teams\\logs");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Teams\\Cache");
            break;
        case CleanupCategory::ZoomCache:
            AddPath(paths, L"%APPDATA%\\Zoom\\bin\\logs");
            AddPath(paths, L"%APPDATA%\\Zoom\\logs");
            break;
        case CleanupCategory::WeChatCache:
            // 只清理缓存目录，保留聊天记录、接收的文件和自定义表情
            AddPath(paths, L"%USERPROFILE%\\Documents\\WeChat Files\\*\\FileStorage\\Cache");
            AddPath(paths, L"%APPDATA%\\Tencent\\WeChat\\Cache");
            break;
        case CleanupCategory::QQCache:
            // 只清理缓存目录，保留聊天记录和接收的文件
            AddPath(paths, L"%USERPROFILE%\\Documents\\Tencent Files\\*\\QQ\\Cache");
            AddPath(paths, L"%APPDATA%\\Tencent\\QQ\\Cache");
            break;
        case CleanupCategory::InstallerCache:
            AddPath(paths, winDir + L"\\Installer\\$PatchCache$");
            AddPath(paths, winDir + L"\\Installer\\SourceHash*");
            break;
        case CleanupCategory::ClashVergeLogs:
            AddPath(paths, L"%APPDATA%\\io.github.clash-verge-rev.clash-verge-rev\\logs");
            break;
        case CleanupCategory::BrowserUpdateCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Google\\Update\\Download");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Edge\\Update\\Download");
            AddPath(paths, sysDir + L"\\config\\systemprofile\\AppData\\Local\\Microsoft\\Edge\\User Data\\*\\Cache");
            break;
        case CleanupCategory::PrintSpooler:
            AddPath(paths, sysDir + L"\\spool\\PRINTERS");
            break;
        case CleanupCategory::EventTraceLogs:
            AddPath(paths, sysDir + L"\\LogFiles\\WMI\\*.etl");
            break;
        case CleanupCategory::RemoteDesktopCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Terminal Server Client\\Cache");
            break;
        case CleanupCategory::DeliveryOptimization:
            AddPath(paths, winDir + L"\\DeliveryOptimization\\*.log");
            AddPath(paths, winDir + L"\\DeliveryOptimization\\Cache");
            break;
        case CleanupCategory::FontCache:
            AddPath(paths, winDir + L"\\ServiceProfiles\\LocalService\\AppData\\Local\\FontCache");
            break;
        case CleanupCategory::NotificationsCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\Notifications");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\WPN");
            break;
        case CleanupCategory::QuickAccessHistory:
            AddPath(paths, L"%APPDATA%\\Microsoft\\Windows\\Recent\\AutomaticDestinations");
            AddPath(paths, L"%APPDATA%\\Microsoft\\Windows\\Recent\\CustomDestinations");
            break;
        case CleanupCategory::ClipboardHistory:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\Clipboard\\History");
            break;
        case CleanupCategory::PerfLogs:
            AddPath(paths, sysDrive + L"PerfLogs");
            break;
        case CleanupCategory::SetupLogs:
            AddPath(paths, winDir + L"\\Logs\\Setup\\*.log");
            AddPath(paths, winDir + L"\\Logs\\SetupCln\\*.log");
            break;
        case CleanupCategory::NetSetupLogs:
            AddPath(paths, winDir + L"\\Logs\\NetSetup\\*.log");
            break;
        case CleanupCategory::RELogs:
            AddPath(paths, sysDir + L"\\Recovery\\*.log");
            break;
        case CleanupCategory::DriverStoreTemp:
            AddPath(paths, sysDir + L"\\DriverStore\\Temp");
            break;
        case CleanupCategory::TempFileExtensions:
            AddPath(paths, sysDrive + L"*.tmp");
            AddPath(paths, sysDrive + L"*.temp");
            AddPath(paths, winDir + L"*.tmp");
            AddPath(paths, winDir + L"*.temp");
            break;
        case CleanupCategory::IconCache:
            AddPath(paths, L"%LOCALAPPDATA%\\IconCache.db");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\iconcache_*");
            break;
        case CleanupCategory::RecentDocs:
            // 只清理 Recent 根目录下的快捷方式，AutomaticDestinations /
            // CustomDestinations 由 QuickAccessHistory 单独处理，避免重复统计
            AddPath(paths, L"%APPDATA%\\Microsoft\\Windows\\Recent\\*.lnk");
            break;
        case CleanupCategory::PowerShellHistory:
            AddPath(paths, L"%APPDATA%\\Microsoft\\Windows\\PowerShell\\PSReadLine\\ConsoleHost_history.txt");
            break;
        case CleanupCategory::INetCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache");
            break;
        case CleanupCategory::ShaderCache:
            AddPath(paths, L"%LOCALAPPDATA%\\NVIDIA\\GLCache");
            AddPath(paths, L"%LOCALAPPDATA%\\NVIDIA\\NV_Cache");
            AddPath(paths, L"%LOCALAPPDATA%\\NVIDIA\\DXCache");
            AddPath(paths, L"%LOCALAPPDATA%\\AMD\\ShaderCache");
            AddPath(paths, L"%LOCALAPPDATA%\\Intel\\ShaderCache");
            AddPath(paths, L"%LOCALAPPDATA%\\D3DSCache");
            break;
        case CleanupCategory::SearchIndex:
            AddPath(paths, L"%ProgramData%\\Microsoft\\Search\\Data\\Applications\\Windows\\*.gthr");
            AddPath(paths, L"%ProgramData%\\Microsoft\\Search\\Data\\Applications\\Windows\\Windows.edb");
            break;
        case CleanupCategory::OfficeDefenderCache:
            AddPath(paths, L"%ProgramData%\\Microsoft\\Office\\*\\Locks");
            AddPath(paths, L"%ProgramData%\\Microsoft\\Windows Defender\\Scans\\History");
            break;
        case CleanupCategory::SystemAccountTemp:
            AddPath(paths, sysDir + L"\\config\\systemprofile\\AppData\\Local\\Temp");
            AddPath(paths, sysDir + L"\\config\\systemprofile\\AppData\\Local\\Microsoft\\Windows\\INetCache");
            break;
        case CleanupCategory::Cookies:
            AddPath(paths, L"%APPDATA%\\Microsoft\\Windows\\Cookies");
            break;
        case CleanupCategory::StoreAppCache: {
            // 逐个包枚举，跳过 Terminal（由 TerminalCache 单独处理）以避免重复统计
            std::wstring packages = ExpandPath(L"%LOCALAPPDATA%\\Packages");
            if (DirectoryExists(packages)) {
                try {
                    for (const auto& entry : fs::directory_iterator(packages,
                            fs::directory_options::skip_permission_denied)) {
                        if (!entry.is_directory()) continue;
                        std::wstring name = entry.path().filename().wstring();
                        if (_wcsicmp(name.c_str(), L"Microsoft.WindowsTerminal_8wekyb3d8bbwe") == 0) {
                            continue;
                        }
                        AddPath(paths, entry.path().wstring() + L"\\LocalCache");
                        AddPath(paths, entry.path().wstring() + L"\\TempState");
                    }
                } catch (...) {}
            }
            break;
        }
        case CleanupCategory::SysWOW64Cache:
            AddPath(paths, winDir + L"\\SysWOW64\\config\\systemprofile\\AppData\\Local\\Temp");
            AddPath(paths, winDir + L"\\SysWOW64\\config\\systemprofile\\AppData\\Local\\Microsoft\\Windows\\INetCache");
            break;
        case CleanupCategory::RootLogFiles:
            AddPath(paths, sysDrive + L"*.log");
            AddPath(paths, winDir + L"*.log");
            break;
        case CleanupCategory::WERFull:
            AddPath(paths, L"%ProgramData%\\Microsoft\\Windows\\WER\\ReportArchive");
            AddPath(paths, L"%ProgramData%\\Microsoft\\Windows\\WER\\ReportQueue");
            AddPath(paths, L"%ProgramData%\\Microsoft\\Windows\\WER\\Temp");
            break;
        case CleanupCategory::AspNetTemp:
            AddPath(paths, winDir + L"\\Microsoft.NET\\Framework\\*\\Temporary ASP.NET Files");
            AddPath(paths, winDir + L"\\Microsoft.NET\\Framework64\\*\\Temporary ASP.NET Files");
            break;
        case CleanupCategory::SystemTempDeep:
            AddPath(paths, winDir + L"\\ServiceProfiles\\LocalService\\AppData\\Local\\Temp");
            AddPath(paths, winDir + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Temp");
            break;
        case CleanupCategory::VSInstallCache:
            AddPath(paths, L"%ProgramData%\\Microsoft\\VisualStudio\\Packages");
            AddPath(paths, L"%ProgramData%\\Microsoft\\VisualStudio\\Setup");
            AddPath(paths, L"%ProgramData%\\Microsoft\\VisualStudio\\Setup\\Backup");
            break;
        case CleanupCategory::VSCodeCache:
            AddPath(paths, L"%APPDATA%\\Code\\Cache");
            AddPath(paths, L"%APPDATA%\\Code\\CachedData");
            AddPath(paths, L"%APPDATA%\\Code\\Code Cache");
            AddPath(paths, L"%APPDATA%\\Code\\GPUCache");
            AddPath(paths, L"%APPDATA%\\Code\\logs");
            AddPath(paths, L"%APPDATA%\\Code\\WorkspacesStorage");
            AddPath(paths, L"%APPDATA%\\Code\\CachedExtensions");
            AddPath(paths, L"%APPDATA%\\Code\\CachedExtensionVSIXs");
            AddPath(paths, L"%APPDATA%\\Code\\Service Worker");
            break;
        case CleanupCategory::JetBrainsCache:
            // 只清理缓存/日志/临时目录，保留用户配置和插件
            AddPath(paths, L"%LOCALAPPDATA%\\JetBrains\\*\\caches");
            AddPath(paths, L"%LOCALAPPDATA%\\JetBrains\\*\\log");
            AddPath(paths, L"%LOCALAPPDATA%\\JetBrains\\*\\tmp");
            AddPath(paths, L"%LOCALAPPDATA%\\JetBrains\\*\\index");
            AddPath(paths, L"%LOCALAPPDATA%\\JetBrains\\*\\compile-server");
            break;
        case CleanupCategory::MsBuildCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\MSBuild");
            break;
        case CleanupCategory::NuGetFallback:
            AddPath(paths, L"%USERPROFILE%\\.nuget\\fallbackpackages");
            break;
        case CleanupCategory::CBSLogs:
            AddPath(paths, winDir + L"\\Logs\\CBS");
            break;
        case CleanupCategory::DISMLogs:
            AddPath(paths, winDir + L"\\Logs\\DISM");
            break;
        case CleanupCategory::IISLogs:
            AddPath(paths, sysDrive + L"inetpub\\logs\\LogFiles");
            break;
        case CleanupCategory::WinSxSBackup:
            AddPath(paths, winDir + L"\\WinSxS\\Backup");
            break;
        case CleanupCategory::DeviceMetadata:
            AddPath(paths, L"%ProgramData%\\Microsoft\\Windows\\DeviceMetadataStore");
            break;
        case CleanupCategory::WindowsUpdateData:
            AddPath(paths, L"%ProgramData%\\Microsoft\\Windows\\WindowsUpdate");
            break;
        case CleanupCategory::OneDriveCache:
            // 只清理缓存和日志，保留账户配置和同步数据库
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\OneDrive\\logs");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\OneDrive\\tmp");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\OneDrive\\cache");
            break;
        case CleanupCategory::OutlookCache:
            // 只清理缓存，保留 OST 离线邮件和账户配置
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Outlook\\RoamCache");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Outlook\\*.log");
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Outlook\\*.tmp");
            break;
        case CleanupCategory::DefenderDefinitionBackup:
            AddPath(paths, L"%ProgramData%\\Microsoft\\Windows Defender\\Definition Updates\\Backup");
            break;
        case CleanupCategory::TerminalCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Packages\\Microsoft.WindowsTerminal_8wekyb3d8bbwe\\LocalCache");
            AddPath(paths, L"%LOCALAPPDATA%\\Packages\\Microsoft.WindowsTerminal_8wekyb3d8bbwe\\TempState");
            break;
        case CleanupCategory::PowerShellCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\PowerShell");
            break;
        case CleanupCategory::ShellCache:
            AddPath(paths, L"%LOCALAPPDATA%\\Microsoft\\Windows\\Shell");
            break;
        // ================================================================
        // AI IDE / Agent tools
        // ================================================================
        case CleanupCategory::ClaudeCodeCache:
            // 保留 settings.json / backups / file-history / sessions / plans
            AddPath(paths, L"%USERPROFILE%\\.claude\\cache");
            AddPath(paths, L"%USERPROFILE%\\.claude\\paste-cache");
            AddPath(paths, L"%USERPROFILE%\\.claude\\telemetry");
            AddPath(paths, L"%USERPROFILE%\\.claude\\projects");
            AddPath(paths, L"%USERPROFILE%\\.claude\\history.jsonl");
            break;
        case CleanupCategory::CursorCache:
            AddPath(paths, L"%APPDATA%\\Cursor\\Cache");
            AddPath(paths, L"%APPDATA%\\Cursor\\CachedData");
            AddPath(paths, L"%APPDATA%\\Cursor\\Code Cache");
            AddPath(paths, L"%APPDATA%\\Cursor\\GPUCache");
            AddPath(paths, L"%APPDATA%\\Cursor\\logs");
            AddPath(paths, L"%APPDATA%\\Cursor\\WorkspacesStorage");
            AddPath(paths, L"%APPDATA%\\Cursor\\CachedExtensions");
            AddPath(paths, L"%APPDATA%\\Cursor\\CachedExtensionVSIXs");
            AddPath(paths, L"%APPDATA%\\Cursor\\Service Worker");
            break;
        case CleanupCategory::WindsurfCache:
            AddPath(paths, L"%APPDATA%\\Windsurf\\Cache");
            AddPath(paths, L"%APPDATA%\\Windsurf\\CachedData");
            AddPath(paths, L"%APPDATA%\\Windsurf\\Code Cache");
            AddPath(paths, L"%APPDATA%\\Windsurf\\GPUCache");
            AddPath(paths, L"%APPDATA%\\Windsurf\\logs");
            AddPath(paths, L"%APPDATA%\\Windsurf\\WorkspacesStorage");
            AddPath(paths, L"%APPDATA%\\Windsurf\\CachedExtensions");
            AddPath(paths, L"%APPDATA%\\Windsurf\\CachedExtensionVSIXs");
            AddPath(paths, L"%APPDATA%\\Windsurf\\Service Worker");
            break;
        case CleanupCategory::TraeCache:
            AddPath(paths, L"%APPDATA%\\Trae\\Cache");
            AddPath(paths, L"%APPDATA%\\Trae\\CachedData");
            AddPath(paths, L"%APPDATA%\\Trae\\Code Cache");
            AddPath(paths, L"%APPDATA%\\Trae\\GPUCache");
            AddPath(paths, L"%APPDATA%\\Trae\\logs");
            AddPath(paths, L"%APPDATA%\\Trae\\WorkspacesStorage");
            AddPath(paths, L"%APPDATA%\\Trae\\CachedExtensions");
            AddPath(paths, L"%APPDATA%\\Trae\\CachedExtensionVSIXs");
            break;
        case CleanupCategory::QoderCache:
            // 只清理缓存/日志/临时/shell 快照，保留 auth/bin/plugins/agents/models/projects
            AddPath(paths, L"%USERPROFILE%\\.qoder\\.cache");
            AddPath(paths, L"%USERPROFILE%\\.qoder\\logs");
            AddPath(paths, L"%USERPROFILE%\\.qoder\\tmp");
            AddPath(paths, L"%USERPROFILE%\\.qoder\\shell-snapshots");
            AddPath(paths, L"%APPDATA%\\Qoder\\Cache");
            AddPath(paths, L"%APPDATA%\\Qoder\\CachedData");
            AddPath(paths, L"%APPDATA%\\Qoder\\logs");
            break;
        case CleanupCategory::ComateCache:
            AddPath(paths, L"%APPDATA%\\Comate\\Cache");
            AddPath(paths, L"%APPDATA%\\Comate\\CachedData");
            AddPath(paths, L"%APPDATA%\\Comate\\Code Cache");
            AddPath(paths, L"%APPDATA%\\Comate\\GPUCache");
            AddPath(paths, L"%APPDATA%\\Comate\\logs");
            AddPath(paths, L"%APPDATA%\\Comate\\WorkspacesStorage");
            AddPath(paths, L"%APPDATA%\\Comate\\CachedExtensions");
            AddPath(paths, L"%APPDATA%\\Comate\\CachedExtensionVSIXs");
            break;
        case CleanupCategory::CopilotChatCache:
            AddPath(paths, L"%APPDATA%\\Code\\User\\globalStorage\\github.copilot-chat");
            AddPath(paths, L"%USERPROFILE%\\.copilot");
            break;
        case CleanupCategory::JetBrainsAICache:
            AddPath(paths, L"%LOCALAPPDATA%\\JetBrains\\*\\AIassistant");
            AddPath(paths, L"%LOCALAPPDATA%\\JetBrains\\*\\plugins\\AIassistant");
            break;
        case CleanupCategory::TabnineCache:
            AddPath(paths, L"%USERPROFILE%\\.tabnine");
            break;
        case CleanupCategory::ContinueCache:
            AddPath(paths, L"%USERPROFILE%\\.continue\\cache");
            AddPath(paths, L"%USERPROFILE%\\.continue\\tmp");
            break;
        case CleanupCategory::ClineCache:
            AddPath(paths, L"%USERPROFILE%\\.cline");
            break;
        case CleanupCategory::RooCache:
            AddPath(paths, L"%USERPROFILE%\\.roo");
            break;
        case CleanupCategory::AiderCache:
            AddPath(paths, L"%USERPROFILE%\\.aider");
            break;
        case CleanupCategory::CodexCache:
            // 保留 auth.json / config.toml / sessions
            AddPath(paths, L"%USERPROFILE%\\.codex\\log");
            AddPath(paths, L"%USERPROFILE%\\.codex\\tmp");
            AddPath(paths, L"%USERPROFILE%\\.codex\\history.jsonl");
            break;
        case CleanupCategory::GeminiCache:
            // 保留 settings.json / credentials
            AddPath(paths, L"%USERPROFILE%\\.gemini\\tmp");
            AddPath(paths, L"%USERPROFILE%\\.gemini\\cache");
            break;
        case CleanupCategory::ZedCache:
            // 保留 settings.json / keymap.json / languages
            AddPath(paths, L"%USERPROFILE%\\.zed\\cache");
            AddPath(paths, L"%USERPROFILE%\\.zed\\tmp");
            AddPath(paths, L"%USERPROFILE%\\.zed\\logs");
            break;
        case CleanupCategory::AugmentCache:
            AddPath(paths, L"%USERPROFILE%\\.augment");
            break;
        case CleanupCategory::LingmaCache:
            AddPath(paths, L"%USERPROFILE%\\.tongyi-lingma");
            break;
        case CleanupCategory::CodeGeeXCache:
            AddPath(paths, L"%USERPROFILE%\\.codegeex");
            break;
        case CleanupCategory::WarpCache:
            // 保留 settings.yaml / keybindings.yaml
            AddPath(paths, L"%USERPROFILE%\\.warp\\cache");
            AddPath(paths, L"%USERPROFILE%\\.warp\\tmp");
            AddPath(paths, L"%USERPROFILE%\\.warp\\logs");
            break;
        // 无文件路径的类别：RecycleBin / MemoryDump / EventLogs / FlushDNS /
        // CrashDumpSettings 由扫描/清理逻辑单独处理
        default:
            break;
    }

    return paths;
}

} // namespace diskcleaner
