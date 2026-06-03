# DiskCleaner - Windows 11 磁盘清理工具

一个使用 C++17 和 Win32 API 开发的 Windows 11 磁盘垃圾清理工具，支持 22 种垃圾类型清理。

## 功能特性

### 支持清理的垃圾类型

| 类别 | 说明 | 风险等级 |
|------|------|----------|
| 用户临时文件 | `%TEMP%` 目录下的临时文件 | 低 |
| 系统临时文件 | `C:\Windows\Temp` 目录 | 低 |
| 预读取文件 | `C:\Windows\Prefetch` 目录 | 高 |
| Windows 更新缓存 | Windows 更新下载的安装包 | 高 |
| 缩略图缓存 | 文件资源管理器缩略图缓存 | 低 |
| Chrome 浏览器缓存 | Google Chrome 缓存文件 | 低 |
| Firefox 浏览器缓存 | Mozilla Firefox 缓存文件 | 低 |
| Edge 浏览器缓存 | Microsoft Edge 缓存文件 | 低 |
| Windows 错误报告 | 错误报告和诊断数据 | 低 |
| 崩溃转储文件 | 应用程序崩溃转储 | 低 |
| 回收站 | 已删除但未清空的文件 | 高 |
| 旧版 Windows 安装 | `Windows.old` 目录 | 高 |
| 休眠文件 | `hiberfil.sys` 文件 | 高 |
| 恢复文件 | 系统恢复文件 | 高 |
| Visual Studio 缓存 | VS 组件缓存、NuGet 缓存 | 中 |
| NuGet 包缓存 | `.nuget\packages` 目录 | 中 |
| npm 缓存 | `npm-cache` 目录 | 中 |
| pip 缓存 | pip 下载包缓存 | 中 |
| Teams 缓存 | Microsoft Teams 缓存和日志 | 低 |
| Zoom 缓存 | Zoom 应用日志 | 低 |
| 微信缓存 | 微信聊天缓存和文件 | 中 |
| QQ 缓存 | QQ 聊天缓存和文件 | 中 |
| 安装程序缓存 | Windows Installer 补丁缓存 | 高 |

### 主要功能

- 扫描并显示各类垃圾文件的大小和数量
- 支持选择性清理（可勾选/取消特定类别）
- 实时显示扫描和清理进度
- 支持 Admin / SYSTEM / TrustedInstaller 三级权限提升
- 高 DPI 感知，PerMonitorV2 支持
- 使用 Seg� UI Variable 字体，清晰渲染

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
2. 程序会自动请求管理员权限（UAC 弹窗）
3. 点击 **Scan** 按钮扫描垃圾文件
4. 勾选要清理的项目
5. 点击 **Clean** 按钮执行清理
6. 确认后开始清理

## 权限提升

程序支持三种权限级别：

| 权限 | 说明 | 用途 |
|------|------|------|
| Admin | 管理员权限（默认） | 清理用户级和部分系统级垃圾 |
| SYSTEM | 系统权限 | 清理更多系统保护文件 |
| TrustedInstaller | 受信任安装程序权限 | 最高权限，可清理几乎所有系统文件 |

## 注意事项

- 清理系统文件需要管理员权限
- 预读取文件会自动跳过最近 24 小时内创建的文件
- 删除 `Windows.old` 后将无法回滚到之前的 Windows 版本
- 清理前请确保没有重要文件在临时目录中
- 微信/QQ 缓存清理前请确认聊天记录已备份

## 项目结构

```
diskcleaner/
├── src/
│   ├── main.cpp          # 程序入口
│   ├── scanner.h/cpp     # 扫描引擎
│   ├── cleaner.h/cpp     # 清理引擎
│   ├── gui.h/cpp         # GUI 界面
│   ├── elevator.h/cpp    # 权限提升
│   ├── types.h           # 类型定义
│   └── utils.h/cpp       # 工具函数
├── resources/
│   ├── app.rc            # 资源文件
│   └── app.manifest      # 应用清单
├── CMakeLists.txt        # CMake 配置
└── README.md             # 说明文档
```

## 技术栈

- C++17
- Win32 API
- Windows Common Controls 6.0
- CMake 构建系统
- UTF-8 BOM 源文件编码

## 许可证

MIT License
