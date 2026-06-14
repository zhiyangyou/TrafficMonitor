# 03 · 数据流：从 Claude Code 发消息到任务栏柱形图变化

> 范围：单条"用户在 Claude Code 客户端发一条消息 → 任务栏 4 个滚动柱形图变化"的端到端追踪。逐跳说明组件、文件路径、关键行号、可能失败的处理。

## 3.1 总体链路（9 跳）

```
[1] Claude Code 调 statusline
        ↓ (stdin JSON, ~数十 ms 一次)
[2] Wrapper 接收 stdin JSON
        ↓ (Console.In.ReadToEnd)
[3] Wrapper 写 sidecar
        ↓ (File.Append + StreamWriter.WriteLine + Flush + Close)
[4] Plugin DataRequired() 触发
        ↓ (1Hz SetTimer, 主程序 1504 行)
[5] CSidecarReader 增量读
        ↓ (DrainNewEntries 返回 vector<StatuslineEntry>)
[6] CPerSessionAccumulator 按 session 维护 state
        ↓ (OnStatuslineUpdate → Delta{input, cache_creation, cache_read, output, dt_ms})
[7] CDataManager 滑动 1s 窗口
        ↓ (累加到 m_window, 剔除 > 1s 前的旧 delta, push 到 4 个环形缓冲)
[8] 4 个 CTokenItem 触发 DrawItem
        ↓ (主程序 Invalidate, TaskBarDlg.cpp:426-457 分支)
[9] 自绘滚动柱形图
        ↓ (GDI FillSolidRect, 1px 列宽 × 60+ 列宽)
```

## 3.2 逐跳详解

### 跳 1 — Claude Code 调 statusline

- **触发**：用户发消息或 Claude Code 推进会话时，Claude Code Desktop 在 Windows 上执行用户在 `%USERPROFILE%\.claude\settings.json` 的 `statusLine.command` 字段指定的命令，把完整 session JSON 通过 stdin pipe 进去。
- **数据形状**（Anthropic 官方 schema，Anthropic 文档）：
  ```json
  {
    "session_id": "sess-abc-123",
    "cwd": "C:\\Users\\me\\projects\\foo",
    "model": { "display_name": "Opus 4.7" },
    "context_window": {
      "context_window_size": 200000,
      "used_percentage": 12,
      "current_usage": {
        "input_tokens": 1234,
        "output_tokens": 56,
        "cache_creation_input_tokens": 7890,
        "cache_read_input_tokens": 50000
      }
    }
  }
  ```
- **失败处理**：`current_usage` 字段为 null（首次 API 调用前、`/compact` 之后）→ Wrapper 写 sidecar 时 `parsed_ok=false`，plugin 跳过。
- **关键行号**：Anthropic 文档（无具体行号）；wrapper 接收端在脚本中用 `[Console]::In.ReadToEnd()`。

### 跳 2 — Wrapper 接收 stdin JSON

- **组件**：PowerShell 5.1+ 脚本 `statusline-wrapper.ps1`，位于 `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1`。
- **入口**：`$json = [Console]::In.ReadToEnd()`。
- **保护**：`if ([string]::IsNullOrWhiteSpace($json)) { exit 0 }`——空 stdin 直接退出。
- **失败处理**：JSON 缺失 → 退出 0，不写 sidecar，plugin 无新数据。

### 跳 3 — Wrapper 写 sidecar

- **组件**：同一 Wrapper 脚本。
- **关键代码**（计划 §5 wrapper 脚本骨架）：
  ```powershell
  $dir = Join-Path $env:APPDATA 'ClaudeTokenMonitor'
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  $sidecar = Join-Path $dir 'sidecar.jsonl'
  $wrapped = ($json.TrimEnd() -replace '}\s*$', '') + ',"wrapper_ms":' + [int64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) + '}'
  $stream = [System.IO.File]::Open($sidecar, 'Append', 'Write', 'Read')
  $writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::UTF8)
  $writer.WriteLine($wrapped)
  $writer.Flush()
  $writer.Close()
  $stream.Close()
  ```
- **追加字段**：在原始 JSON 末尾补 `wrapper_ms`（Unix 毫秒），不破坏原始 payload。
- **原子性**：单 write + flush + close 是一行原子追加——多 Claude Code 进程同时写最差产生半行，被 `json::parse` 拒绝后跳过。
- **pipe 给原 statusline**（计划 §5 第二段）：从 `~/.claude/settings.json` 读 `statusLine.originalCommand`，把原 stdin 通过 temp 文件 + `cmd /c "type <tmp> | <orig>"` pipe 给原命令。`$ErrorActionPreference='SilentlyContinue'` 保证原命令失败不影响本次 sidecar 写入。
- **失败处理**：`%APPDATA%` 不可写 → `New-Item` 抛错但被 `SilentlyContinue` 吞掉，下游 plugin 看不到新数据。

### 跳 4 — Plugin DataRequired() 触发

- **组件**：主程序 1Hz `MONITOR_TIMER` 定时器。
- **关键行号**：`TrafficMonitor/TrafficMonitorDlg.cpp:1500-1519`：
  ```cpp
  for (const auto& plugin_info : theApp.m_plugins.GetPlugins()) {
      if (plugin_info.plugin != nullptr) {
          plugin_info.plugin->DataRequired();  // ← line 1504
          ITMPlugin::MonitorInfo monitor_info;
          // ... 填充 11 个字段 ...
          plugin_info.plugin->OnMonitorInfo(monitor_info);
      }
  }
  ```
- **频率**：1Hz（由 `m_general_data.monitor_time_span` 决定，默认 1000ms，范围 200-30000）。
- **`DataRequired()` 内部**：`CClaudeTokenMonitorPlugin::DataRequired()` 调 `CDataManager::Instance().Tick()`。
- **线程**：采集线程 `MonitorThreadCallback`（`TrafficMonitorDlg.cpp:1527-1554`）。

### 跳 5 — CSidecarReader 增量读

- **组件**：`CDataManager::Tick()` 内调用 `m_reader.DrainNewEntries()`。
- **打开方式**：`CreateFile` 共享读模式（`FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`），允许多 Wrapper 进程同时写。
- **增量机制**：组件内部保存 `m_last_offset`（上次读到 EOF 的字节偏移），每次 `DrainNewEntries` 从该偏移读到当前 EOF，按 `\n` 分行。
- **解析**：每行尝试 `nlohmann::json::parse`，失败则该行 `parsed_ok=false`。
- **返回**：`vector<StatuslineEntry>`，按文件出现顺序排列。
- **失败处理**：
  - sidecar 文件被删/缺失 → `CreateFile` 失败，函数返回空 vector，下游 `Tick()` 不更新窗口
  - 半行 JSON → `parse` 抛错被捕获，标记 `parsed_ok=false`，跳过该行
  - 文件 > 5MB → `Tick()` 触发 rotate（关闭 → 改名 `.old` → 重开），`m_last_offset` 归零

### 跳 6 — CPerSessionAccumulator 按 session 维护 state

- **组件**：`CDataManager::Tick()` 遍历 `DrainNewEntries()` 返回的 `entries`，对每条：
  ```cpp
  key = { e.session_id, e.cwd }
  d = m_acc.OnStatuslineUpdate(key, e.input, e.output, e.cache_creation, e.cache_read, now_ms)
  if (d.dt_ms == 0) continue;  // first_seen
  ```
- **SessionKey 构成**：`{session_id, cwd}` 组合（不用单一 session_id，规避 Claude Code Desktop 偶尔重用 session_id 但 cwd 不同的边角案例）。
- **OnStatuslineUpdate 内部**（计划 §4.2 步骤 1-3）：
  - 若 `key` 不在 `m_states` 中 → 新建 `SessionState{first_seen=true}`，返回 0 delta
  - 若 `key` 已存在 → 用本次 4 个 token 数减去 `state.last_input` 等 4 个字段得到 `Delta{input, cache_creation, cache_read, output}`，把 `dt_ms = now_ms - state.last_seen_ms`，更新 `state.last_*` 与 `state.last_seen_ms`
- **失败处理**：
  - 同 session 两次 statusline 的 4 个 token 数都相同 → `Delta` 全为 0，正常累加（不出现"未变化"异常）
  - 上次 token 数 > 本次 token 数（计数器重置或 `/compact` 之后）→ `Delta` 为负，正常累加，柱形图短暂下凹后恢复
- **清理**：`m_acc.ForgetInactiveSessions(now_ms, 600000)` 清理 10 分钟无更新的 session。

### 跳 7 — CDataManager 滑动 1s 窗口

- **组件**：同一 `Tick()`。
- **数据流**（计划 §4.2 步骤 1-6）：
  ```
  // 2. 累加到 1 秒窗口
  m_window.input       += d.input
  m_window.cache_creation += d.cache_creation
  m_window.cache_read  += d.cache_read
  m_window.output      += d.output
  // 3. 滑动窗口：剔除 > 1s 前的旧 delta
  while m_recent_deltas.front.ts_ms < now_ms - 1000:
      m_window -= m_recent_deltas.front.delta
      m_recent_deltas.pop_front()
  m_recent_deltas.push_back({now_ms, d})  // 实际按 entry 推入
  // 6. 归一化（0~1）入环形缓冲
  m_input_history.Push(Normalize(m_window.input, m_max_in, 100))
  // 4 个类推
  ```
- **归一化基线**：`m_max_*` 是 60s 滑动最大值，`floor 100 tok/s` 防 0 除。
- **汇总过滤**：`ShouldAggregate(key)` 按 `SettingData.aggregate_mode` 决定 key 是否计入 speed_* 汇总（仅 `m_max_*` 滑动需要；环形缓冲始终推入 4 个 item 的全量）。
- **格式化**：`FormatValueText()` 把 `m_window.input` 等 4 个数值格式化为 `"12.3k/s"` 等字符串，写到 `m_input_value_text` 等 4 个 `CString` 缓存。
- **首帧保护**：`m_last_tick_ms == 0` → 只设基线，不读 sidecar，不动窗口（避免插件首次 `LoadLibrary` 时累积的旧 delta 一次性冲爆柱形图）。

### 跳 8 — 4 个 CTokenItem 触发 DrawItem

- **触发链**：`DataRequired()` 末尾 `m_window` 变化 → 主程序 `SendMessage(WM_MONITOR_INFO_UPDATED)` → `CTrafficMonitorDlg::OnMonitorInfoUpdated` → `Invalidate()` 任务栏窗口 → `CTaskBarDlg::OnPaint` → `ShowInfo` → `DrawPluginItem`。
- **关键行号**：`TrafficMonitor/TaskBarDlg.cpp:381-485` 的 `CTaskBarDlg::DrawPluginItem`：
  - 插件 item `IsCustomDraw()==true` 时（line 426）进入自绘分支
  - 计算背景色亮度（line 429-430）：`(GetRValue(bk) + GetGValue(bk) + GetBValue(bk)) / 3`，< 128 → `dark_mode=true`
  - 通过 `drawer.ExecuteGdiOperation(rect, [...](HDC gdi_dc){ item->DrawItem(gdi_dc, ...); })` 桥接到 D2D 上下文（line 448-455）；GDI 后端直接 `p_dc->GetSafeHdc()` 传入（line 441-442）
  - 5 个参数：`hDC, x, y, w, h, dark_mode`
- **插件接口**：`include/PluginInterface.h:76` 的 `IPluginItem::DrawItem`。
- **宽度来源**：`GetItemWidth()` 返回 96 DPI 下的最小宽度；主程序按当前 DPI 放大后传入 `w`。

### 跳 9 — 自绘滚动柱形图

- **组件**：`CTokenItem::DrawItem(hDC, x, y, w, h, dark_mode)`。
- **算法**（计划 §4.3）：
  ```cpp
  text_w = 40  // 右侧 40px 数字
  bar_w = w - text_w
  bar_rect = CRect(x, y, x + bar_w, y + h)
  text_rect = CRect(x + bar_w, y, x + w, y + h)
  dc = CDC::FromHandle(hDC)
  n = m_history.Size()
  for i in [0, n):
      v = m_history.At(i)  // 0=oldest, n-1=newest
      col_x = bar_rect.right - (i + 1)
      col_h = int(v * bar_rect.Height())
      col_y_top = bar_rect.bottom - col_h
      dc.FillSolidRect(CRect(col_x, col_y_top, col_x + 1, bar_rect.bottom), color)
  dc.SetTextColor(value_text_color)
  dc.DrawText(m_value_text, text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE)
  ```
- **数据源**：`m_history` 是 `CRingBuffer<float, N>`，归一化 0~1，由 `CDataManager::Tick()` 在跳 7 push。
- **颜色**：由构造参数指定的 `COLORREF`（来自 `SettingData.color_input` 等 4 个字段）；`dark_mode` 切换深/浅色变体。
- **数字**：`m_value_text` 是 `CDataManager` 算好的 `"12.3k/s"`，`GetItemValueText()` 直接返回（自绘模式下文本 getter 被忽略，但接口仍要返回非空）。

## 3.3 关键不变量

- **线程边界**：跳 4-7 在采集线程；跳 8-9 在主 UI 线程。`m_window` 与 `m_history` 的写入侧有锁（计划未明确锁，由 `Tick()` 单线程 + 主程序 SendMessage 触发绘制保证）。
- **时间单调性**：`now_ms = GetTickCount64()` 用于窗口滑动；`wrapper_ms` 仅用于新鲜度检查（> 5min 丢弃），不参与 delta 计算。
- **配置不重启**：`SettingData` 改完（`COptionsDlg::IDOK`）即时生效，环形缓冲不重置——下一帧 `Tick()` 用新 `aggregate_mode` / 颜色过滤。
- **`DrainNewEntries` 幂等性**：连续两次调用若 sidecar 无新内容，第二次返回空 vector，`Tick()` 不增 `m_window`。

## 3.4 关键文件索引

- `CDataManager::Tick()` 主循环：计划 §4.2
- 主程序 1Hz 定时器调 `DataRequired()`：`TrafficMonitor/TrafficMonitorDlg.cpp:1500-1519`
- 主程序插件绘制分发：`TrafficMonitor/TaskBarDlg.cpp:381-485`
- `IPluginItem::DrawItem` 接口：`include/PluginInterface.h:76`
- `IsCustomDraw` 接口：`include/PluginInterface.h:53`
- Wrapper 脚本骨架：计划 §5
