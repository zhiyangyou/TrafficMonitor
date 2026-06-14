# Risk and Edge Cases

当前实现下已识别 12 个风险点和各自的缓解手段。所有缓解都是已经写在代码里的处理路径，不包含"将来要做"。

## 风险与缓解对照表

| # | 风险 | 缓解 |
|---|---|---|
| 1 | sidecar 多 writer 并发（多个 Claude Code 进程同时 append） | PowerShell wrapper 用单 `WriteLine` + `Flush` + `Close` 在 NTFS 上对单行写入原子；最差情况两行被交错成半行 JSON，`nlohmann::json::parse` 拒绝 → 该行 `parsed_ok=false` → `Tick()` 跳过；`m_last_offset` 仍前进，不会卡在坏行 |
| 2 | `current_usage` 字段为 null（首次 API 调用前 / `/compact` 之后） | `CSidecarReader` 解析时检查 `entry.contains("context_window") && entry["context_window"].contains("current_usage") && !entry["context_window"]["current_usage"].is_null()`，任一不满足 → `parsed_ok=false` → 跳过该条 |
| 3 | wrapper 未装 / 被反病毒删 / 用户禁用 PowerShell | sidecar 文件不存在 → `CSidecarReader::DrainNewEntries()` 返回空 list → `DataManager::Tick()` 早返回，4 个柱形图保持上次值，不清零；`COptionsDlg` 状态条显示"wrapper not installed"，用户从对话框重新装 |
| 4 | 主程序反复 `FreeLibrary` / `LoadLibrary` 插件（设置改后重载） | `CClaudeTokenMonitorPlugin::OnInitialize` 在每次重新加载时调 `DataManager::Instance().Reset()`，里面调 `m_acc.Reset()` 清空 `SessionState` map + `m_reader.ResetOffset()` 把 `m_last_offset` 置 0 + `m_recent_deltas.clear()` 清空滑动窗口 + 4 个 `m_*_history` 清空 + 4 个 `m_max_*` 重置为 100 floor。下一帧 Tick 重新建立 baseline |
| 5 | sidecar 文件 5MB+ 不清理 | `DataManager::Tick()` 每次 `DrainNewEntries()` 后调 `m_reader.MaybeRotate(5 * 1024 * 1024)`：检查 `GetFileSizeEx` > 5MB → `CloseHandle` → `MoveFileEx(sidecar.jsonl, sidecar.jsonl.old, MOVEFILE_REPLACE_EXISTING)` → 重开 handle。`.old` 文件不自动删，留给用户手动清 |
| 6 | 时间漂移（`wrapper_ms` Unix 毫秒 vs `GetTickCount64` 单调时钟） | `wrapper_ms` 只用作新鲜度判定辅助字段（> 5min 的 entry 丢弃，不用入 `m_acc`）；delta 计算与滑动窗口完全基于本进程 `GetTickCount64`，不受 wall clock 跳变影响 |
| 7 | 用户没装 Claude Code（`~/.claude/settings.json` 不存在） | `CStatuslineInstaller::CheckInstalled()` 返回 `ClaudeCodeMissing`；`COptionsDlg` 状态条显示 "Claude Code not detected"；Install 按钮 disable（`EnableWindow(FALSE)`） |
| 8 | `~/.claude/settings.json` 只读（IT 部门锁定 / 文件 ACL） | `CStatuslineInstaller::Install()` 调 `CreateFile` 测试写权限；写失败 → 返回 `SettingsReadOnly`，对话框弹错误"无法写入 settings.json，请检查权限"，原文件不动；不备份、不写 wrapper |
| 9 | 开了 5+ session 时 `COptionsDlg` session 列表太长 | `DataManager::ListRecentSessions(int lookback_ms = 24 * 3600 * 1000)`：扫 sidecar 全量历史，过滤 `last_seen_ms >= now_ms - 24h`；超过 24h 没 statusline 更新的 session 不出现在 SINGLE 下拉和 ignored 列表 |
| 10 | sidecar 一行非法 JSON（半行 / 截断 / 编码错） | `CSidecarReader` 解析时 `try { json::parse(line) } catch (...) { entry.parsed_ok=false; }`，catch 后继续读下一行；`m_last_offset` 仍前进到该行末尾，不会卡死 |
| 11 | 删 sidecar 中途（用户 / 清扫软件） | `CSidecarReader` 每次 `DrainNewEntries()` 检查 `m_last_offset > current_eof` → 视为 rotate/删，`m_last_offset = 0` 从头读新文件（重置后第一帧 Tick 重建 baseline，`first_seen=true` 路径保证不产假 delta）。`MaybeRotate` 重命名时如果原文件被外部删，`MoveFileEx` 失败 → 忽略，重开空文件 |
| 12 | 多次 Install wrapper 互相覆盖（用户重装 / 旧 wrapper 残留） | `CStatuslineInstaller::Install()` 每次执行 `settings.json.bak.<unix-ts>` 备份覆盖式命名（最新一份），旧 `.bak.<ts>` 文件保留在磁盘供用户回滚（实际上现在不会被清，最坏情况是占用磁盘）；wrapper 脚本每次 `WriteAllText` 覆盖写，不会出现老版本残留；`previous-statusline.txt` 每次 Install 重新写（用户对原始 statusline 的引用在每次安装时被快照） |

## 各风险的详细处理路径

### 风险 1 — sidecar 多 writer 并发

主进程在 `Claude Code Desktop` 上可以多开多个会话窗口，每个窗口在每个回合结束时独立调 statusline wrapper。wrapper 是独立 PowerShell 进程，对 `sidecar.jsonl` 用 `[File]::Open(path, Append, Write, Read)` 拿独占写 append 句柄；同一进程的连续两次 write 中 `Flush` + `Close` 之间不会被插入其他进程的字节。NTFS 对单行 ≤ 4KB 的 write 是原子的（`MAXIMUM_WAIT_OBJECTS` 等价约束在文档里没明确但实测成立）。最差场景两个 wrapper 进程同时 `Flush`，各写 200 字节的一行，可能被 OS 拆成交错的两段 → 半行 JSON → `nlohmann::json::parse` 抛异常 → catch 块把 `parsed_ok=false`，`m_last_offset` 前进到该行末尾。下一次 `DrainNewEntries()` 跳过该行继续读下一行，不会卡死。

### 风险 2 — `current_usage` 字段为 null

Claude Code 在以下时刻 push 一个 `context_window.current_usage = null` 的 statusline JSON：
- 首次启动还没发过任何 API 请求
- `/compact` 命令执行完毕后的下一个回合
- `/clear` 命令后的回合

`CSidecarReader` 解析时显式判 `is_null()`，因为 nlohmann 对 JSON `null` 解析成 `json::value_t::null` 但访问其字段会抛 `type_error`。显式判 null 让 `entry.parsed_ok=false`，`DataManager::Tick()` 跳过这条 entry，对应 session 的 `first_seen` 状态保持不变（baseline 不会被 null 污染）。

### 风险 3 — wrapper 未装 / 被反病毒删

用户场景：刚装插件还没点 Install；或者某些 AV（Windows Defender Smartscreen / McAfee）把 `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1` 当可疑脚本删掉。

检测路径：`CSidecarReader::DrainNewEntries()` 调 `CreateFile(sidecar, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_ALWAYS)`，文件不存在时返回 `INVALID_HANDLE_VALUE` → `DrainNewEntries()` 返回空 list → `Tick()` 早返回。

UI 路径：`COptionsDlg` 状态条读 `CStatuslineInstaller::CheckInstalled()` 的结果，区分显示三态："wrapper not installed, click to install" / "wrapper installed" / "Claude Code not detected"。

### 风险 4 — 主程序反复 LoadLibrary/FreeLibrary

主程序设置里有"重载所有插件"的功能（`TrafficMonitor` 设置 → 插件 → 重载），触发 `FreeLibrary` 当前 DLL → `LoadLibrary` 同一个 DLL 重新执行 `DllMain` → 走 `OnInitialize`。如果 `m_acc` 不重置，旧的 `SessionState` baseline 跟新进程对不上（不同进程的 `GetTickCount64` 基线不同），第一帧 `current - baseline` 会爆。

处理：每次 `OnInitialize` 第一行 `DataManager::Instance().Reset()`，内部清空所有可变状态：
- `m_acc.m_states.clear()`
- `m_reader.m_last_offset = 0`
- `m_recent_deltas.clear()`
- `m_window = {0,0,0,0}`
- `m_*_history` 4 个环形缓冲 `Clear()`
- `m_max_*` 4 个滑动最大重置 `{value=100, ts_ms=0}`

下一帧 Tick 第一条 entry 走 `first_seen=true` 路径建立新 baseline。

### 风险 5 — sidecar 5MB+ 不清理

每次 statusline 推送约 300-500 字节，5MB ≈ 10000-16000 条 entry。重度用户一天可能产生 ~5000 条（每分钟一条对话节奏）。文件不清理会持续增长。

处理：`CSidecarReader::MaybeRotate(uint64_t threshold)` 在每次 `DrainNewEntries()` 末尾调用：
1. `GetFileSizeEx(handle, &size)` 读当前大小
2. `size.QuadPart > threshold` → 关闭 handle → `MoveFileExW(L"sidecar.jsonl", L"sidecar.jsonl.old", MOVEFILE_REPLACE_EXISTING)` → 重开 handle（`OPEN_ALWAYS` 模式创建空文件）
3. `m_last_offset = 0` 从新空文件读

`.old` 文件不自动清，留给用户。如果用户想手动清，路径在 `%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl.old`。

### 风险 6 — 时间漂移

`wrapper_ms` 是 PowerShell `[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()`，依赖 wall clock。本进程用 `GetTickCount64()`，单调但仅 49.7 天回卷。

只把 `wrapper_ms` 用于"entry 新鲜度"判定（`wrapper_ms > now_unix - 300000` 才接受），避免 wall clock 跳变（用户手动改时间、夏令时、NTP 校时）污染 delta 计算。所有 delta 与滑动窗口完全基于 `GetTickCount64`，跨 wall clock 跳变不受影响。

### 风险 7 — 用户没装 Claude Code

`CheckInstalled()` 检查 `PathFileExists(L"%USERPROFILE%\\.claude\\settings.json")`。文件不存在 → 返回 `ClaudeCodeMissing` 状态码。`COptionsDlg` 拿到这个状态码 disable Install 按钮（`GetDlgItem(IDC_BTN_INSTALL)->EnableWindow(FALSE)`），状态条显示 "Claude Code not detected"。

### 风险 8 — settings.json 只读

`Install()` 第一步 `CreateFile(settings.json, GENERIC_WRITE, FILE_SHARE_READ, OPEN_EXISTING)` 测试写权限。返回 `INVALID_HANDLE_VALUE` 且 `GetLastError() == ERROR_ACCESS_DENIED` → 返回 `SettingsReadOnly`，对话框弹 `MessageBox` 显示错误信息，用户点 OK 退出。原文件不动，不写 wrapper 脚本，不创建 sidecar。

### 风险 9 — 5+ session 时列表过长

用户同时跑多个 Claude Code workspace 会产生多个 `session_id`（按 cwd 区分）。`COptionsDlg` 每次打开都调 `ListRecentSessions(24h)`，扫 sidecar 全文，提取 `session_id` 去重集合，按 `last_seen_ms` 倒序排，过滤 24h 外的。

SINGLE 下拉最多显示最近 24h 的 session；ignored 列表同理。24h 外的 session 仍在 `m_acc` 里参与聚合（直到 10 分钟无更新被 `ForgetInactiveSessions` 清），只是不出现在 UI 选择列表。

### 风险 10 — sidecar 一行非法 JSON

`CSidecarReader` 用 `std::getline` 按 `\n` 切行，逐行 `json::parse(line)`。半行 / 截断 / 编码错（UTF-8 BOM 错位、JSON 字符未转义）→ parse 抛 `json::parse_error` → catch 块设 `parsed_ok=false` → `m_last_offset` 前进到 `\n` 之后 → 继续读下一行。

实测触发：用户中途 kill Claude Code 主进程，wrapper 写到一半被杀，留下半行。重启插件后这条半行被跳过，下一条完整行正常解析。

### 风险 11 — 删 sidecar 中途

用户清理磁盘 / IT 部门组策略 / 杀毒软件可能随时删 sidecar。`DrainNewEntries()` 每次调用 `GetFileSizeEx` → 若 `size < m_last_offset`（不可能正常发生，只有文件被 truncate 或删后新建一个更小的）→ 重置 `m_last_offset = 0` → 从头读。

如果文件被删后未新建 → `CreateFile` 返回 `INVALID_HANDLE_VALUE` → 直接返回空 list。

`MaybeRotate` 重命名时如果原文件已被删 → `MoveFileEx` 返回 0 → 不报错 → 重开 handle 创建空文件。

### 风险 12 — 多次 Install wrapper 覆盖

每次 `Install()` 流程：
1. 备份当前 `settings.json` 到 `settings.json.bak.<unix_ts>`（unix_ts = 当前 Unix 秒，多次安装文件名不同，旧 .bak 文件不被覆盖）
2. 解析 JSON，写 `previous-statusline.txt`（覆盖）
3. 写 `statusLine.command` 指向 wrapper（原子写：`.tmp` → rename）
4. 写 wrapper `.ps1`（覆盖）

幂等性：连续点 Install 100 次，每次都能正确备份 + 覆盖。`previous-statusline.txt` 始终保存"用户原始 statusline"的快照，即使中间用户自己改了 `statusLine`，wrapper 还原时仍然还原到第一次安装前的原始命令。

## 当前没有自动化处理的

- `sidecar.jsonl.old` 文件累积：rotate 后 `.old` 文件保留在 `%APPDATA%\ClaudeTokenMonitor\`，不在插件自清理范围。用户在 `COptionsDlg` 没有"清旧数据"按钮，需要手动 `%APPDATA%` 下删除。
- 反病毒软件杀 wrapper 脚本：某些 AV 会标记 `%APPDATA%\<random>\*.ps1` 为可疑。检测方式是 `sidecar.jsonl` 长期不增长（`Tick()` 早返回），但插件无法直接区分"用户没开 Claude Code"和"wrapper 被杀"。当前不报警。
- `nlohmann/json` 单头文件 ~25k 行编译时间：DLL 首次编译 ~3-5s 增量每次 < 1s，在可接受范围。
- 多 session 同时 `/compact`：每个 session 独立触发 baseline 重置，不互相影响。
- 任务栏窗口 0 宽度（DPI 缩放异常）：`DrawItem` 收到 `w=0` 时直接 return，不画。

## 历史风险已经废弃但保留记录

- "用 `total_input_tokens` / `total_output_tokens` 做数据源" — 已被废弃（v2.1.132 后 `total_*` 语义改为"当前上下文窗口"），见 `data-source.md`。
- "通过主程序 `TryDrawGraph` 画滚动柱形图" — 主程序路径对插件是坏的，见 `custom-draw.md`。
- "在 `OnInitialize` 自动装 wrapper" — 改为手动，避免用户没装 Claude Code 时弹错误。
- "用 EMA 平滑 token 速度" — 改为 1Hz 滑动窗口原始脉动，决策见计划文件第 7 行"速度定义 = 过去 1 秒的 delta tokens（不 EMA 平滑）"。