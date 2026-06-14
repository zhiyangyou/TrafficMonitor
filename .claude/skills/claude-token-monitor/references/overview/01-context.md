# 01 · 术语表 / CONTEXT

> 范围：本项目（ClaudeTokenMonitor 插件）独有的术语。每个术语给"是什么"+ "避免使用"，格式与 `grill-with-docs-lite` 的 CONTEXT-FORMAT.md 一致。
> 通用编程概念（线程、指针、DLL、CString 等）不收录。

## 1. IPluginItem

- **是什么**：TrafficMonitor 插件协议定义的"一个显示项"接口（`include/PluginInterface.h:9`）。插件通过 `ITMPlugin::GetItem(int index)` 暴露 0..N 个实例，每个实例对应任务栏/主窗口上的一格。`IsCustomDraw()` 返回 `true` 时由插件自己绘制，否则主程序读 `GetItemLableText()` + `GetItemValueText()` 文本（接口 line 53、76）。
- **避免使用**：插件项、UI item、plugin widget、显示节点。

## 2. CTokenItem

- **是什么**：本项目定义的 `IPluginItem` 实现基类。计划文件 §2 模块拆分里登记的 4 个 item（Token In / Cache Write / Cache Read / Token Out）各自由 `CTokenItem` 的派生（或同一基类的 4 个实例）承担，具体数值类型与颜色由构造参数决定。4 个 item 都把 `IsCustomDraw` 重写为 `true`，并实现 `DrawItem` 自绘滚动柱形图。
- **避免使用**：Token widget、Token 控件、Bar 控件、Chart item。

## 3. CSidecarReader

- **是什么**：本项目定义的 JSONL tail 读取组件，负责 `CreateFile` 共享读模式打开 sidecar 文件，按 EOF 偏移追踪增量内容并按行解析 `StatuslineEntry`。`DrainNewEntries()` 每次调用返回自上次读取以来新增的解析后条目集合；解析失败行被跳过，不抛出。
- **避免使用**：Tailer、log reader、file watcher、JSON 解析器（最后一项不准确——它做的是"增量读 + 解析"两件事，但名称上不要简化为"解析器"）。

## 4. CPerSessionAccumulator

- **是什么**：本项目定义的"按 session 累计 token 增量"组件。对每个 `SessionKey{session_id, cwd}` 维护一份 `SessionState{last_input, last_cache_creation, last_cache_read, last_output, last_seen_ms, session_start_ms, first_seen}`。`OnStatuslineUpdate()` 接收本次 statusline 读到的 4 个 token 数，返回本次产生的 `Delta`。`ForgetInactiveSessions(now_ms, ttl_ms=600000)` 清理 10 分钟内未更新的 session。
- **避免使用**：Aggregator、token counter、session manager（最后一项过于宽泛，掩盖了"delta 计算"这一精确职责）。

## 5. StatuslineEntry

- **是什么**：sidecar 一行 JSON 解析后的内存结构。字段：`session_id`、`cwd`、`model`、`input`、`output`、`cache_creation`、`cache_read`、`received_ms`（本进程读到的 wall clock）、`parsed_ok`（true/false）。`parsed_ok=false` 表示该行 JSON 损坏或字段缺失，`CDataManager::Tick()` 跳过。
- **避免使用**：Statusline JSON、statusline record、entry point、log line。

## 6. Delta

- **是什么**：一次 statusline 更新相对于"上次见到该 session 时记录的 4 个 token 累计值"产生的增量。结构：`{input, cache_creation, cache_read, output, dt_ms}`，4 个 token 字段是 `long long`，`dt_ms` 是本次更新与上次更新的毫秒差。`first_seen=true` 时返回的 `Delta` 4 个 token 字段全为 0、`dt_ms=0`，`CDataManager::Tick()` 检测到 `dt_ms==0` 后 `continue`。
- **避免使用**：Token 差值、increment、token delta（英文里"token delta"与本项目同名概念不完全一致——Anthropic 自己的 `cache_creation_input_tokens` 也叫 token delta，避免与 Anthropic 文档术语混用）。

## 7. SettingData

- **是什么**：本项目插件配置数据结构。字段：`aggregate_mode`（默认 `AggregateMode::ALL`）、`single_session_id`、`active_window_ms`（默认 60000）、`ignored_sessions`（vector<wstring>）、4 个 `COLORREF` 颜色（`color_input`、`color_cache_creation`、`color_cache_read`、`color_output`）、`refresh_interval_ms`（默认 1000，UI 暴露为 hint）、`wrapper_path_override`（默认空 → `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1`）。由 `CDataManager` 单例持有；通过 `<config_dir>\ClaudeTokenMonitor.dll.ini` 持久化（参考 `PluginDemo/DataManager.cpp:27-35` 的 INI 加载模式）。
- **避免使用**：config、preferences、options、setting（与主程序 `CommonData.h` 里的 `TaskBarSettingData` 等结构体在术语上区分开——主程序用 `SettingData` 是基于 `CommonData.h` 的命名空间上下文，本插件的 `SettingData` 是独立结构体）。

## 8. AggregateMode

- **是什么**：枚举类 `enum class AggregateMode { ALL, ACTIVE, SINGLE }`，定义本插件在汇总多个 session 时的过滤策略。
  - `ALL`：累加所有 `CPerSessionAccumulator` 维护的 session（受 `ignored_sessions` 黑名单约束）
  - `ACTIVE`：只累加 `last_seen_ms >= now_ms - active_window_ms` 的 session（默认 60s 窗口）
  - `SINGLE`：只累加 `single_session_id` 指定的那一个 session
- **避免使用**：Filter mode、聚合模式（中文译名可接受但代码内用 `AggregateMode`）、scope mode。

## 9. Wrapper

- **是什么**：一段 PowerShell 5.1+ 脚本，文件名 `statusline-wrapper.ps1`，由 `CStatuslineInstaller::Install()` 写入 `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1`（从 DLL 嵌入的字符串字面量写出）。功能：把 Claude Code 传入的 statusline JSON 一边写到 sidecar，一边 pipe 给用户原 statusline（`statusLine.originalCommand`）。
- **避免使用**：Wrapper script（冗余）、PowerShell 脚本、shell script、hook 脚本（与 IAT hook 概念混用）、proxy script。

## 10. Sidecar

- **是什么**：JSONL 文件，路径 `%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl`。Wrapper 把每次 statusline 调用追加一行（每行一个 JSON 对象 + 末尾 `wrapper_ms` 时间戳）。Plugin 端 `CSidecarReader` 以共享读模式打开。约定：单 write + flush + close 行原子（PowerShell `[System.IO.File]::Open(..., 'Append', 'Write', 'Read')` + `StreamWriter.WriteLine` + `Flush` + `Close`）。
- **避免使用**：log file、JSONL log、sidecar log、sidecar file、auxiliary file。

## 11. First-seen baseline

- **是什么**：`CPerSessionAccumulator` 的状态机语义——首次见到某 `SessionKey` 时，`SessionState::first_seen=true`，本次 `OnStatuslineUpdate()` 返回的 `Delta.dt_ms=0`、4 个 token 字段全为 0。目的是不把"打开 session 那一刻的 4 个累计值"误算成"瞬间消耗的 delta"。`Tick()` 跳过 `dt_ms==0` 的 delta 直接推入环形缓冲。
- **避免使用**：Initial baseline、first sample、warmup、calibration（"calibration"暗示调参，与本语义无关）。

## 12. Token category

- **是什么**：本项目将 Anthropic `current_usage.*` 的 4 个字段映射成 4 个类别：
  - `input` ← `current_usage.input_tokens`
  - `cache_creation` ← `current_usage.cache_creation_input_tokens`
  - `cache_read` ← `current_usage.cache_read_input_tokens`
  - `output` ← `current_usage.output_tokens`
- **避免使用**：token type、token kind、metric（最后一项过于宽泛，掩盖了"4 类分别计"的决策）。

## 13. Dark mode（自绘上下文）

- **是什么**：`IPluginItem::DrawItem(hDC, x, y, w, h, dark_mode)` 的 `dark_mode` 布尔判定。**定义**：由主程序 `TaskBarDlg.cpp:430` 计算背景色亮度 `(GetRValue(bk) + GetGValue(bk) + GetBValue(bk)) / 3 < 128` 得到，传给插件。本插件的 `CTokenItem::DrawItem` 用此 flag 切深/浅色（颜色也由 `SettingData` 4 个 `COLORREF` 覆盖）。
- **避免使用**：Dark theme、dark mode 标志、theme flag。

## 14. 待澄清

- （暂无）
