#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

namespace diskcleaner {

// File size representation
struct FileSize {
    int64_t bytes = 0;
    std::wstring toString() const;
    double toMB() const { return bytes / (1024.0 * 1024.0); }
    double toGB() const { return bytes / (1024.0 * 1024.0 * 1024.0); }
};

std::wstring FormatBytes(int64_t bytes);
std::wstring GetEnvVar(const std::wstring& name);
std::wstring ExpandPath(const std::wstring& path);
std::wstring GetWindowsDir();
std::wstring GetSystemDir();
std::wstring GetSystemDrive();
bool DirectoryExists(const std::wstring& path);
bool FileExists(const std::wstring& path);

// 展开路径中的通配符（支持 * 和 ?，可出现在任意段），返回所有实际存在的路径。
// 会自动展开环境变量。中间段只保留目录，最后一段保留文件与目录。
std::vector<std::wstring> ExpandGlob(const std::wstring& pattern);

// 统计目录大小。cancel 非空时可在遍历中协作式取消。
int64_t GetDirectorySize(const std::wstring& path, int64_t* fileCount = nullptr,
                         const std::atomic<bool>* cancel = nullptr);
bool DeleteDirectoryContents(const std::wstring& path);
bool DeleteDirectoryRecursive(const std::wstring& path);
bool DeleteFileSafe(const std::wstring& path);
bool EmptyRecycleBin(HWND hwnd = nullptr);
bool IsRunAsAdmin();
bool RequestAdminPrivileges();
std::wstring GetLastErrorString();

// System command execution helpers
// timeoutMs: 等待子进程退出的毫秒数，超时后强制终止
bool ExecuteSystemCommand(const std::wstring& command, const std::wstring& args,
                          DWORD timeoutMs = 30000);
bool ClearEventLogs();
bool FlushDNSCache();
bool DisableHibernate();
bool StopServiceByName(const std::wstring& serviceName);
// 发送停止请求并等待服务真正进入 SERVICE_STOPPED，最多等待 timeoutMs
bool StopServiceAndWait(const std::wstring& serviceName, DWORD timeoutMs = 15000);
bool StartServiceByName(const std::wstring& serviceName);
bool IsServiceRunning(const std::wstring& serviceName);

// Registry helpers
bool SetRegistryValue(const std::wstring& keyPath, const std::wstring& valueName, DWORD valueType, const BYTE* data, DWORD dataSize);
bool GetRegistryValue(const std::wstring& keyPath, const std::wstring& valueName, std::wstring& outValue);

// System Restore helpers (系统还原点保护)
// 检查系统保护是否已启用
bool IsSystemRestoreEnabled();
// 创建系统还原点，返回序列号（0 表示失败）
int64_t CreateSystemRestorePoint(const std::wstring& description);
// 打开系统还原界面（rstrui.exe）
bool OpenSystemRestore();

} // namespace diskcleaner