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