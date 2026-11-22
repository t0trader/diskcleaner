# 变更记录

## [未发布] - 2026-11-20

### 新增：智能清理（Smart mode）

- 时间闸门：只删除超过阈值（默认 24 小时，可选 3/7/30 天或关闭）未修改的文件。扫描与清理共用同一策略，界面显示的可清理大小即实际可删除量，避免误删正在写入的临时文件与活跃缓存。
- 应用占用感知：新增类别→进程名映射（msedge/chrome/firefox/Code/Cursor/Trae/Qoder CN/JetBrains/Weixin…）。扫描时列表标记 `[in use]`，清理时整类跳过并在确认框与完成提示中说明。
- 新增 `Browser Service Worker Cache` 类别：覆盖 Edge、Edge SxS、Chrome（含 Beta/Dev/SxS）、Brave、Vivaldi、Chromium 的 `Service Worker\CacheStorage` 与 `ScriptCache`。这是浏览器占用磁盘的大头（实测单机可达 3.6 GB），此前完全未覆盖。
- 新增 `WeChat Media Cache` 类别：微信 4.x 的 `xwechat\log` 与 `radium\web`，保留 `radium\users`（聊天数据）与 `xplugin`（插件）。
- 站点级归因：解析 CacheStorage 哈希目录的 `index.txt` 得到站点域名；无域名时从缓存条目开头抽样 URL 取众数。详情窗口按站点展示大小与最近使用时间，支持按站点删除。
- 详情窗口：任意类别可查看 Top 10 大子目录/文件（只读）。
- 安全护栏：`Login Data` / `Cookies` / `Bookmarks` / `Preferences` / `Local Storage` / `IndexedDB` / `*.ost` / `settings.json` 等受保护名永不删除；盘符根、用户目录根、Windows 系统目录、目录联接拒绝作为删除目标；只删目录内容，不删类别根目录；CacheStorage 的 `index.txt` 保留以维持站点注册。
- GUI：新增 Smart mode 开关、时间阈值下拉与 Details 按钮；设置持久化到 `HKCU\Software\JCleaner`。

### 修复 / 调整

- `TraeCache` 修正为同时匹配 `Trae CN`（国内版真实目录名）。
- `QoderCache` 修正为 `.qoder-cn`（logs/tmp/.cache）与 `%APPDATA%\com.qodercn.app.stable`，保留 bin/plugins/projects/memories。
- `EdgeCache` 补齐 Edge SxS 与 `Crashpad`、`component_crx_cache`、`ProvenanceData`。
- 清理引擎改为逐文件过滤删除：不再整目录 `remove_all`，未到阈值的文件与被占用文件会保留，空子目录自动移除。
- 完成提示新增 “kept N recent files / protected N files / N categories skipped (app running)”。
- 修复详情窗口关闭导致整个程序退出的问题：原实现用 `PostQuitMessage` 结束模态循环，会把主窗口的消息循环一并终止；改为 `PostThreadMessage(WM_APP+2)` 私有通知，且不再依赖 `IsWindow` 判断退出。
- 详情窗口加载改为窗口显示后再统计（`WM_APP+1`），避免打开瞬间卡顿；新增重入保护，连点 Details 不会叠加多个窗口。
- 修复扫描与清理阈值不一致：服务托管类别（搜索索引/字体缓存/Windows Update/打印队列）在扫描侧此前仍按 24h 过滤，导致显示 0 且无法清理；现在两侧统一不做时间过滤。
- 修复关闭 Smart mode 时 Prefetch 的行为不一致：扫描仍保留 24h 规则、清理却全删；现统一为始终至少保留 24h。
- `Cleaner` 删除单个文件前同样执行危险根路径校验；`CacheStorage` 统计增加文件大小/时间的错误码保护，避免异常值污染显示。
- Smart mode 阈值下拉对无法识别的注册表值回落到 24 小时。

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
- 首次打开时窗口居中显示在鼠标所在显示器的工作区。
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

- 新增应用图标：咖啡色渐变背景 + `JCleaner` 字标，提供 16/32/64/128 四种尺寸（小尺寸使用 J 字标），已嵌入 exe、窗口和任务栏；生成脚本位于 [tools/make_icon.py](tools/make_icon.py)。
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
