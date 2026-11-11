# 变更记录

## [未发布] - 2026-11-11

本次为代码审查后的修复与增强，覆盖扫描、清理、权限提升、GUI、构建配置与文档。
版本基线：1.0.0。未创建提交、未推送。

### 修复

#### 扫描引擎（[src/scanner.cpp](src/scanner.cpp)、[src/scanner.h](src/scanner.h)）
- 修复 `QoderCache` 从未被扫描的问题：移除手写类别数组，改为遍历 `CleanupCategory::Count`，避免今后新增类别再次遗漏。
- 修复 `MEMORY.DMP` 文件本身未被统计的问题，现在同时统计该文件与 `Minidump`、`LiveKernelReports` 目录。
- 修复 Prefetch 扫描大小与清理规则不一致的问题：扫描时同样跳过最近 24 小时内使用的文件。
- 重新扫描时保留用户手动勾选/取消的选择，不再被默认勾选状态覆盖。
- `CrashDumpSettings` 默认不勾选，避免默认清理时静默修改系统设置。
- Claude Code、Codex CLI 会删除会话/命令历史，风险等级由中调整为高，默认不勾选。
- 完成回调改为在互斥锁外触发快照，避免回调方调用 `GetResults()` 时死锁。

#### 清理引擎（[src/cleaner.cpp](src/cleaner.cpp)、[src/cleaner.h](src/cleaner.h)）
- 只累计真正删除成功的字节数与文件数，删除失败不再虚报释放量。
- 新增 skipped 计数，清理完成时提示被占用而跳过的文件数量。
- 只读文件自动清除只读属性后重试删除；失败时恢复原属性。
- 目录删除失败时清除树内只读属性后重试，且不跟踪符号链接。
- 修复 MemoryDump 清理时重复累加扫描大小的问题。
- 休眠文件关闭成功后按扫描大小计入释放量。
- 打印后台、Windows Update 改为统一走可统计的目录清理流程。
- `CrashDumpSettings` 的 WER `Disabled` 值由 0（启用）修正为 1（禁用）。
- 跳过扫描结果为 0 的文件类类别，避免无意义地停止/重启系统服务。
- 清理进度回调做 100ms 节流，避免大量小文件刷爆 UI 消息队列。

#### 权限提升（[src/elevator.cpp](src/elevator.cpp)、[src/elevator.h](src/elevator.h)）
- 修复 SYSTEM / TrustedInstaller 提权始终失败的问题：原实现把空字符串当作 exe 路径。现在自动解析当前模块路径并传入 `--elevated`。
- 提权成功后退出原进程，避免同时存在两个窗口；失败时显示具体错误并恢复下拉框。
- 修复 TrustedInstaller 服务启动失败时的忙等死循环，改为带 20 秒超时的轮询。
- 修复 TrustedInstaller 识别：同时检查 `TokenUser` 与 `TokenGroups` 中的 TI SID，不再误判为 Admin。
- 修复 `AdjustTokenPrivileges` 使用过期 `GetLastError` 判断结果的问题。
- 修复 `winlogon.exe` 枚举中 `pUserSid` 为空时的潜在崩溃。
- 启动提权进程不再使用 `CREATE_NEW_CONSOLE`，避免 GUI 程序额外弹出控制台窗口。
- 移除 `std::format` 依赖，改用 `std::to_wstring`。

#### GUI（[src/gui.cpp](src/gui.cpp)、[src/gui.h](src/gui.h)）
- 扫描/清理过程中按钮改为 **Cancel** 并保持可用，支持真正的协作式取消。
- 提权切换（Admin → SYSTEM / TrustedInstaller）时传递当前窗口位置、大小与最大化状态，新实例在原位置显示，不再跳到屏幕其他位置。
- 修复 Clean 按钮清理结束后仍停留在 "Cleaning..." 的问题。
- 取消后丢弃队列中残留的进度消息，避免旧状态覆盖 "cancelled" 提示。
- 修复 `Select All` 使用过期内部状态的问题，改为读取复选框实际状态。
- 列表重建期间抑制 `LVN_ITEMCHANGED`，避免控件中间状态覆盖扫描结果。
- 高 DPI 下窗口初始尺寸、边距、按钮、状态栏、列宽全部按 DPI 缩放；启动时读取实际窗口 DPI。
- `DefWindowProc` 默认分支改用传入的 `HWND`，不再使用成员变量。
- 清理确认框列出高风险/系统设置类类别；完成提示包含 skipped 数量。
- 缓存清理总量，避免每条进度消息都遍历并加锁全部扫描结果。
- 修正零大小但需执行系统操作的类别（DNS/事件日志/崩溃转储设置等）被错误禁用的问题。

#### 构建与配置（[CMakeLists.txt](CMakeLists.txt)、[resources/app.manifest](resources/app.manifest)、[src/main.cpp](src/main.cpp)）
- 修复 `app.manifest` 从未嵌入 exe 的问题：longPathAware、PerMonitorV2、SegmentHeap、supportedOS 之前全部缺失。
- 清单改为 `asInvoker` 以避免与链接器生成的 UAC 片段合并冲突；启动后仍由 `main.cpp` 调用 `ShellExecuteEx("runas")` 主动请求管理员权限。
- Release 配置不再覆盖 CMake 默认的 `/MD /O2 /DNDEBUG`，改为追加 `/GL /GF /Gy` 与 `/LTCG`。
- 移除与清单重复的 `VS_DPI_AWARE` 属性。
- `main.cpp` 将 SYSTEM / TrustedInstaller 令牌也视为已提权，避免重复触发 UAC。
- `ExpandPath` / `GetEnvVar` 改为动态长度缓冲，支持超过 `MAX_PATH` 的路径。
- `CreateProcessW` 使用可写的 `std::wstring::data()` 缓冲区。
- 清除事件日志前启用 `SeSecurityPrivilege`，使安全日志也可被清除。

### 变更与增强

- 新增 [src/category_paths.h](src/category_paths.h) / [src/category_paths.cpp](src/category_paths.cpp)：扫描与清理共用同一份类别路径表，消除约 500 行重复代码和两处漂移风险。
- 类别内路径按大小写不敏感去重，修复 `%TEMP%` 与 `%LOCALAPPDATA%\Temp` 指向同一目录时大小翻倍的问题。
- 系统级路径改用 `%ProgramData%` / `%SystemDrive%` 展开，不再硬编码 `C:\`。
- 新增 NVIDIA `DXCache`、`D3DSCache` 着色器缓存路径。
- Recent Documents 仅清理 Recent 根目录下的 `*.lnk`，与 Quick Access History 的跳转列表不再重复统计。
- Store App Cache 枚举时排除 Windows Terminal 包，Terminal 缓存补充 `TempState`，消除交叉重复。
- Visual Studio 缓存不再重复包含 NuGet 缓存（由 NuGet Cache 类别单独负责）。
- README 同步更新：C++23、取消能力、统计口径、DPI 缩放、统一路径表、清单与提权说明。

### 行为变化说明

- 清理统计口径变为“实际删除成功”，因此显示的释放量可能低于旧版本。
- 扫描结果为 0 的文件类类别不会执行清理，减少无意义的服务重启。
- Claude Code / Codex CLI 历史记录默认不勾选；CrashDumpSettings 默认不勾选。
- Recent Documents 不再删除 Quick Access 的跳转列表目录。
- 双击启动仍会自动弹出 UAC，但方式改为进程内主动请求。

### 验证

- `cmake -G "Visual Studio 18 2026" -A x64` 配置成功。
- `Release` 与 `Debug` x64 构建均通过。
- 使用 `mt.exe` 提取 exe 内嵌清单，确认 PerMonitorV2、longPathAware、SegmentHeap、supportedOS 已生效。
- 独立测试程序验证：
  - 95 个类别全部参与扫描，进度回调 95 次；
  - 类别名称、描述、风险等级无缺失；
  - 抽检类别路径无重复；
  - Qoder Cache 能正确扫描到（本机约 37 MB）；
  - UserTemp 去重后仅保留 1 条路径。
- 未执行 GUI 交互点测（需交互式 UAC），未创建提交或推送。
