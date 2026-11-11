# DiskCleaner - Windows 11 磁盘清理工具

一个使用 C++23 和 Win32 API 开发的 Windows 11 磁盘垃圾清理工具，支持 50+ 种垃圾类型清理。

基于 WinCleaner.ps1 脚本重构，保留了所有核心清理功能，并提供了图形化界面和三级权限提升机制。

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
| **新增模块 (来自 WinCleaner.ps1)** | | |
| Clash Verge 日志 | 代理应用日志 | 低 |
| 浏览器更新缓存 | Chrome/Edge 更新下载缓存 | 低 |
| 打印后台队列 | 待打印的假脱机文件 | 低 |
| 事件追踪日志 | WMI .etl 跟踪日志 | 低 |
| 远程桌面缓存 | TSC 客户端缓存 | 低 |
| 传递优化 | Windows 传递优化缓存 | 低 |
| 字体缓存 | Windows 字体服务缓存 | 低 |
| 通知推送缓存 | Windows 通知数据库 | 低 |
| 快速访问历史 | 资源管理器快速访问记录 | 低 |
| 剪贴板历史 | Windows 剪贴板历史数据 | 低 |
| PerfLogs | 性能监控日志目录 | 低 |
| Windows Setup 日志 | 系统安装日志 | 低 |
| NetSetup 日志 | 网络设置日志 | 低 |
| Windows RE 日志 | 恢复环境日志 | 低 |
| 驱动包临时文件 | 驱动解压临时文件 | 低 |
| 临时扩展名文件 | 根目录 .tmp/.temp 文件 | 低 |
| 事件日志 | 全部 Windows 事件日志 (wevtutil) | 高 |
| 图标缓存 | 资源管理器图标缓存数据库 | 低 |
| 最近文档 | 开始菜单最近文档快捷方式 | 低 |
| PowerShell 历史 | PSReadLine 命令历史 | 中 |
| 临时 Internet 文件 | IE/Edge 遗留临时文件 | 低 |
| GPU 着色器缓存 | NVIDIA/AMD/Intel 着色器编译缓存 | 中 |
| 搜索索引缓存 | Windows 搜索索引数据库 | 高 |
| Office & Defender | Office 锁文件和 Defender 扫描历史 | 中 |
| 系统账户临时文件 | SYSTEM 账户临时文件 | 低 |
| Cookies | 用户系统 Cookie | 低 |
| 商店应用缓存 | MS Store 应用本地缓存 | 低 |
| 32位应用缓存 | SysWOW64 应用临时文件 | 低 |
| MEMORY.DMP | 系统内存转储文件 | 低 |
| 根目录日志文件 | C:\ 和 C:\Windows\ 下的 .log 文件 | 低 |
| 完整 WER 报告 | Windows 错误报告完整数据 | 低 |
| ASP.NET 临时文件 | 编译后的 ASP.NET 临时文件 | 低 |
| 深度系统临时文件 | ServiceProfiles 账户临时文件 | 低 |
| DNS 缓存 | DNS 解析器缓存 | 中 |
| **AI IDE / 智能体工具** | | |
| Claude Code 缓存 | cache、paste-cache、telemetry、项目会话记录、命令历史（保留 settings/backups/sessions） | 高 |
| Cursor 缓存 | 编辑器缓存、日志、工作区存储、扩展缓存 | 中 |
| Windsurf 缓存 | 编辑器缓存、日志、工作区存储、扩展缓存 | 中 |
| Trae 缓存 | 编辑器缓存、日志、工作区存储、扩展缓存 | 中 |
| Qoder 缓存 | 阿里 Qoder 编辑器缓存、日志、工作区存储 | 中 |
| Comate 缓存 | 百度 Comate 编辑器缓存、日志、工作区存储 | 中 |
| Copilot Chat 缓存 | GitHub Copilot Chat 扩展缓存 | 中 |
| JetBrains AI 缓存 | JetBrains AI Assistant 缓存（保留 IDE 配置） | 中 |
| Tabnine 缓存 | Tabnine 本地模型与缓存 | 中 |
| Codex CLI 缓存 | OpenAI Codex 日志/临时/历史（保留 auth/config） | 高 |
| Continue 缓存 | Continue 扩展缓存与临时目录 | 低 |
| Cline 缓存 | Cline 扩展缓存 | 低 |
| Roo Code 缓存 | Roo Code 扩展缓存 | 低 |
| Aider 缓存 | Aider 助手缓存 | 低 |
| Gemini CLI 缓存 | Gemini CLI 临时/缓存（保留 settings/credentials） | 低 |
| Zed 缓存 | Zed 编辑器缓存/临时/日志（保留 settings） | 低 |
| Augment 缓存 | Augment Code 扩展缓存 | 低 |
| 通义灵码缓存 | 通义灵码缓存文件 | 低 |
| CodeGeeX 缓存 | CodeGeeX 扩展缓存 | 低 |
| Warp 终端缓存 | Warp 终端缓存/临时/日志（保留 settings） | 低 |

### 主要功能

- 扫描并显示各类垃圾文件的大小和数量
- 支持选择性清理（可勾选/取消特定类别）
- 实时显示扫描和清理进度，扫描/清理过程中按钮变为 **Cancel**，可随时中断
- 清理统计只累计真正删除成功的文件；被占用的文件会跳过并在完成提示中给出数量
- 只读文件自动清除只读属性后重试删除，删除失败则恢复原属性
- 支持 Admin / SYSTEM / TrustedInstaller 三级权限提升
- 高 DPI 感知，PerMonitorV2 支持，窗口、控件、列宽全部按 DPI 缩放
- **通配符路径智能展开**：支持 `*`、`?` 通配符出现在任意路径段，自动枚举所有匹配项（如 `%LOCALAPPDATA%\Packages\*\LocalCache`、`C:\Windows\Logs\Setup\*.log`）
- **统一路径表**：扫描与清理共用同一份类别路径定义，并对同一类别内的重复路径去重（避免 `%TEMP%` 与 `%LOCALAPPDATA%\Temp` 重复统计）
- **服务停止等待**：清理 Windows Update / 字体缓存 / 搜索索引 / 打印后台前，等待服务真正停止，避免文件占用导致清理失败
- **事件日志 API**：使用 Windows Event Log API 枚举并清除所有事件日志，无需解析命令行输出
- **协作式取消**：扫描和清理过程可中断，超大目录遍历支持取消
- **长路径感知**：启用 `longPathAware`，环境变量展开与文件操作均支持超过 MAX_PATH 的路径
- 使用 Segoe UI Variable 字体，清晰渲染
- 应用图标：咖啡色渐变背景 + `JCleaner` 字标，提供 16/32/64/128 多尺寸（小尺寸使用 J 字标），已嵌入 exe、窗口与任务栏

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

程序清单使用 `asInvoker`（避免与链接器生成的 UAC 片段冲突），启动后由 `main.cpp` 通过 `ShellExecuteEx("runas")` 主动请求管理员权限；选择 SYSTEM/TrustedInstaller 会启动一个新的提权实例并退出当前进程。

## 注意事项

- 清理系统文件需要管理员权限
- 预读取文件会自动跳过最近 24 小时内创建的文件（扫描大小同样排除这些文件）
- 删除 `Windows.old` 后将无法回滚到之前的 Windows 版本
- 清理前请确保没有重要文件在临时目录中
- 微信/QQ 仅清理缓存目录，不会删除聊天记录和接收的文件
- OneDrive/Outlook 仅清理缓存，保留账户配置和 OST 离线邮件
- AI IDE 缓存清理仅针对缓存/临时目录，保留用户配置；Claude Code 会话记录、Codex 历史等可选清理，请确认后勾选
- “Crash Dump Settings” 会写入注册表关闭崩溃转储与 WER，默认不勾选
- 被其他进程占用的文件无法删除，会在清理完成提示中计入 skipped 数量

## 项目结构

```
diskcleaner/
├── src/
│   ├── main.cpp          # 程序入口
│   ├── scanner.h/cpp     # 扫描引擎
│   ├── cleaner.h/cpp     # 清理引擎
│   ├── category_paths.h/cpp # 清理类别路径表（扫描/清理共用）
│   ├── gui.h/cpp         # GUI 界面
│   ├── elevator.h/cpp    # 权限提升
│   ├── types.h           # 类型定义
│   └── utils.h/cpp       # 工具函数
├── resources/
│   ├── app.ico           # 应用图标（16/32/64/128）
│   ├── app.rc            # 资源文件
│   └── app.manifest      # 应用清单
├── tools/
│   └── make_icon.py      # 图标生成脚本（Python + Pillow）
├── CMakeLists.txt        # CMake 配置
└── README.md             # 说明文档
```

## 技术栈

- C++23 (`/std:c++latest`)，Release 使用 `/O2 /GL /LTCG`
- Win32 API
- Windows Common Controls 6.0
- CMake 构建系统
- UTF-8 源文件编码（`/utf-8`）
- 通配符路径展开 (`FindFirstFileW` + 自定义 `ExpandGlob`)，类别内路径去重
- 服务控制 API (`StopServiceAndWait` / `StartServiceByName`)
- Windows Event Log API (`EvtOpenChannelEnum` / `EvtClearLog`)，自动启用 SeSecurityPrivilege
- 系统命令执行 (powercfg, ipconfig，带超时保护)
- 注册表 API (SetRegistryValue)
- 删除重试：清除只读属性后重删，失败恢复属性并计入 skipped
- 内嵌应用清单：PerMonitorV2、longPathAware、SegmentHeap、supportedOS

## 许可证

MIT License
