#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

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
bool DirectoryExists(const std::wstring& path);
bool FileExists(const std::wstring& path);
int64_t GetDirectorySize(const std::wstring& path, int64_t* fileCount = nullptr);
bool DeleteDirectoryContents(const std::wstring& path);
bool DeleteDirectoryRecursive(const std::wstring& path);
bool DeleteFileSafe(const std::wstring& path);
bool EmptyRecycleBin(HWND hwnd = nullptr);
bool IsRunAsAdmin();
bool RequestAdminPrivileges();
std::wstring GetLastErrorString();

} // namespace diskcleaner