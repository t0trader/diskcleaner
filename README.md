# DiskCleaner - Windows 11 磁盘清理工具

一个使用 C++ 和 Win32 API 开发的 Windows 11 磁盘垃圾清理工具。

## 功能特性

### 支持清理的垃圾类型

| 类别 | 说明 | 风险等级 |
|------|------|----------|
| 用户临时文件 | `%TEMP%` 目录下的临时文件 | 低 |
| 系统临时文件 | `C:\Windows\Temp` 目录 | 低 |
| 预读取文件 | `C:\Windows\Prefetch` 目录 | 低 |
| Windows更新缓存 | Windows更新下载的安装包 | 中 |
| 缩略图缓存 | 文件资源管理器缩略图缓存 | 低 |
| Chrome浏览器缓存 | Google Chrome 缓存文件 | 低 |
| Firefox浏览器缓存 | Mozilla Firefox 缓存文件 | 低 |
| Edge浏览器缓存 | Microsoft Edge 缓存文件 | 低 |
| Windows错误报告 | 错误报告和诊断数据 | 低 |
| 崩溃转储文件 | 应用程序崩溃转储 | 低 |
| 回收站 | 已删除但未清空的文件 | 中 |
| 旧版Windows安装 | `Windows.old` 目录 | 高 |

### 主要功能

- 扫描并显示各类垃圾文件的大小和数量
- 支持选择性清理（可勾选/取消特定类别）
- 实�示扫描和清理进度
- 自动请求管理员权限以清理系统文件
- 支持高 DPI 显示

## 系统要求

- Windows 10/11
- Visual Studio 2022 或更高版本（带 C++ 桌面开发工作负载）
- CMake 3.20+

## 构建方法

```powershell
cd D:\srccode\vibecode\diskcleaner
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

构建完成后，可执行文件位于 `build/bin/Release/DiskCleaner.exe`。

## 使用方法

1. 双击运行 `DiskCleaner.exe`
2. 程序会自动请求管理员权限（UAC弹窗）
3. 点击"Scan"按钮扫描垃圾文件
4. 勾选要清理的项目
5. 点击"Clean"按钮执行清理

## 使用方法

1. 以管理员身份运行 `DiskCleaner.exe`（程序会自动请求管理员权限）
2. 点击"开始扫描"按钮
3. 等待扫描完成，查看各类垃圾文件统计
4. 勾选要清理的项目
5. 点击"立即清理"按钮
6. 确认后开始清理

## 注意事项

- 清理系统文件需要管理员权限
- 建议不要删除预读取文件中的最近24小时内文件
- 删除 `Windows.old` 后将无法回滚到之前的 Windows 版本
- 清理前请确保没有重要文件在临时目录中

## 项目结构

```
diskcleaner/
├── src/
│   ├── main.cpp          # 程序入口
│   ├── scanner.h/cpp     # 扫描引擎
│   ├── cleaner.h/cpp     # 清理引擎
│   ├── gui.h/cpp         # GUI界面
│   └── utils.h/cpp       # 工具函数
├── resources/
│   ├── app.rc            # 资源文件
│   └── app.manifest      # 应用清单
├── CMakeLists.txt        # CMake配置
└── README.md             # 说明文档
```

## 技术栈

- C++17
- Win32 API
- Windows Common Controls
- CMake 构建系统