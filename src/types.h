#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace diskcleaner {

enum class CleanupCategory {
    UserTemp = 0,
    SystemTemp,
    Prefetch,
    WindowsUpdate,
    ThumbnailCache,
    ChromeCache,
    FirefoxCache,
    EdgeCache,
    ErrorReports,
    CrashDumps,
    RecycleBin,
    WindowsOld,
    HibernationFile,
    RecoveryFiles,
    // IDE & Dev tools
    VisualStudioCache,
    NuGetCache,
    NpmCache,
    PipCache,
    // Applications
    TeamsCache,
    ZoomCache,
    WeChatCache,
    QQCache,
    InstallerCache,
    // PowerShell script modules (from WinCleaner.ps1)
    ClashVergeLogs,           // 1. Clash Verge 事件日志
    BrowserUpdateCache,       // 6. 浏览器更新缓存 (Google/Edge Download)
    PrintSpooler,             // 9. 打印后台缓存
    EventTraceLogs,           // 13. WMI 事件追踪日志 (.etl)
    RemoteDesktopCache,       // 14. 远程桌面缓存 (TSCache)
    DeliveryOptimization,     // 16. 传递优化缓存
    FontCache,                // 18. 字体缓存 (FontCache service)
    NotificationsCache,       // 19. 用户通知和推送缓存
    QuickAccessHistory,       // 20. 快速访问历史记录 (AutomaticDestinations)
    ClipboardHistory,         // 22. 剪贴板历史
    PerfLogs,                 // 23. C:\PerfLogs
    SetupLogs,                // 24. Windows Setup 日志
    NetSetupLogs,             // 25. NetSetup 日志
    RELogs,                   // 26. Windows RE 日志
    DriverStoreTemp,          // 27. 驱动包临时缓存
    TempFileExtensions,       // 28. 根目录临时扩展文件 (*.tmp *.temp)
    EventLogsWEvtUtil,        // 29. Windows 事件日志 (wevtutil 清除)
    IconCache,                // 31. 图标缓存 (IconCache.db)
    RecentDocs,               // 32. 最近使用的文档列表
    PowerShellHistory,        // 33. PowerShell 历史记录 (PSReadLine)
    INetCache,                // 34. 临时 Internet 文件 (INetCache)
    ShaderCache,              // 36. 显卡着色器缓存 (AMD/NVIDIA/Intel)
    CrashDumpSettings,        // 37. 崩溃转储注册表设置
    SearchIndex,              // 10. 搜索索引缓存
    OfficeDefenderCache,      // 11. Office 和 Defender 缓存
    SystemAccountTemp,        // 39. 系统账户临时文件
    Cookies,                  // 40. Cookies 清理
    StoreAppCache,            // 41. 商店应用缓存 (Packages\LocalCache)
    SysWOW64Cache,            // 42. 32位应用缓存 (SysWOW64)
    MemoryDump,               // 47. MEMORY.DMP 系统内存转储
    RootLogFiles,             // 48. 系统根目录日志文件 (*.log)
    WERFull,                  // 49. 完整 WER 崩溃报告 (含 Temp)
    AspNetTemp,               // 51. 临时 ASP.NET 文件
    SystemTempDeep,           // 53. 临时系统文件深度清理 (ServiceProfiles)
    FlushDNS,                 // 15. DNS 缓存刷新
    // Extended categories
    VSInstallCache,           // VS 安装源缓存 (ProgramData)
    VSCodeCache,              // VS Code 缓存和日志
    JetBrainsCache,           // JetBrains IDE 缓存
    MsBuildCache,             // MSBuild 缓存
    NuGetFallback,            // NuGet 回退包
    CBSLogs,                  // CBS 组件服务日志
    DISMLogs,                 // DISM 部署映像服务日志
    IISLogs,                  // IIS 日志
    WinSxSBackup,             // WinSxS 备份
    DeviceMetadata,           // 设备元数据
    WindowsUpdateData,        // Windows Update 数据
    OneDriveCache,            // OneDrive 缓存
    OutlookCache,             // Outlook 缓存
    DefenderDefinitionBackup, // Defender 定义备份
    TerminalCache,            // Windows Terminal 缓存
    PowerShellCache,          // PowerShell 缓存
    ShellCache,               // Shell 缓存
    // AI IDE / Agent tools (智能体 IDE 临时目录)
    ClaudeCodeCache,          // Claude Code 会话记录/shell 快照/todos/statsig
    CursorCache,              // Cursor 编辑器缓存
    WindsurfCache,            // Windsurf 编辑器缓存
    TraeCache,                // Trae 编辑器缓存
    QoderCache,               // Qoder (阿里巴巴) 编辑器缓存
    ComateCache,              // 百度 Comate 编辑器缓存
    CopilotChatCache,         // GitHub Copilot Chat 缓存
    JetBrainsAICache,         // JetBrains AI Assistant 缓存
    TabnineCache,             // Tabnine 缓存
    ContinueCache,            // Continue 缓存
    ClineCache,               // Cline 缓存
    RooCache,                 // Roo Code 缓存
    AiderCache,               // Aider 缓存
    CodexCache,               // OpenAI Codex CLI 缓存
    GeminiCache,              // Gemini CLI 缓存
    ZedCache,                 // Zed 编辑器缓存
    AugmentCache,             // Augment Code 缓存
    LingmaCache,              // 通义灵码缓存
    CodeGeeXCache,            // CodeGeeX 缓存
    WarpCache,                // Warp 终端缓存
    Count
};

// Risk level for cleanup items
enum class RiskLevel {
    Low,      // Safe to delete, no impact on system/software
    Medium,   // May need re-download/rebuild, but no core impact
    High      // May affect system functionality or cannot rollback
};

struct CleanupItem {
    CleanupCategory category = CleanupCategory::UserTemp;
    RiskLevel risk = RiskLevel::Low;
    std::wstring name;
    std::wstring description;
    std::wstring path;
    int64_t size = 0;
    int64_t fileCount = 0;
    bool enabled = true;
    bool scanned = false;
};

} // namespace diskcleaner