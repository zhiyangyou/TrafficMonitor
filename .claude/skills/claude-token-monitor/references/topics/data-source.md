# Data Source

唯一数据源 = statusline sidecar（`%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl`）。Claude Code 主进程在每次回合把完整 session JSON pipe 给用户配置的 statusline 脚本；wrapper 把这串 JSON 追加写到 sidecar JSONL，本插件只读 sidecar。

## 为什么不直接读 Claude Code 的 JSONL 日志

Claude Code CLI 在 `~/.claude/projects/<encoded-cwd>/*.jsonl` 写日志；Claude Code Desktop for Windows 在 `%APPDATA%/Claude/local-agent-mode-sessions/**/projects/<dir>/*.jsonl` 写日志。两条路径下 `usage.input_tokens` / `usage.output_tokens` 都是流式占位符，不能作为计费/速度数据源。

实测偏差（gille.ai 在 2024-2025 多轮采样）：
- `input_tokens` 实测偏低 100-174 倍
- `output_tokens` 实测偏低 10-17 倍
- 约 75% 的 `input_tokens` 字段值是 0 或 1（占位符，未填真实值）

引用：
- https://gille.ai/en/blog/claude-code-jsonl-logs-undercount-tokens/
- https://github.com/anthropics/claude-code/issues/28197

结论：JSONL 不可用。statusline pipe 是唯一可信源。

## statusline JSON schema（Anthropic 官方）

Claude Code 在每个回合结束时把 statusline JSON pipe 给用户在 `~/.claude/settings.json` 里配置的脚本。顶级字段：

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

四类 token 字段都在 `context_window.current_usage` 下：
- `input_tokens` — 本回合新增的裸 input（不计 cache）
- `output_tokens` — 本回合新增的 output
- `cache_creation_input_tokens` — 本回合写入 cache 的 token
- `cache_read_input_tokens` — 本回合从 cache 读出的 token

## 为什么用 `current_usage`，不用 `total_*`

v2.1.132 之前：CLI 还在输出 `total_input_tokens` / `total_output_tokens` 字段，但口径在多版本之间漂移，不能保证。

v2.1.132 之后：`total_*` 字段语义变为"当前上下文窗口内的累计"，不再是"整个 session 的累计"。`total_*` 不能用来算增量。

`current_usage.*` 在每个回合结束时由 Claude Code 重置为该回合的"上下文窗口当前快照"，所以"本次比上次多了多少"才是真正的本回合 token 消耗增量。这是本插件用 `current_usage` 而不用 `total_*` 的原因。

## sidecar 文件格式

路径：`%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl`

格式：JSONL（JSON Lines）。每行一个对象，对应一次 statusline 推送。wrapper 在原始 JSON 末尾追加一个 `wrapper_ms` 字段（`[int64]` Unix 毫秒），不改原 JSON 的任何键。

示例一行：

```json
{"session_id":"sess-abc","cwd":"C:\\foo","model":{"display_name":"Opus 4.7"},"context_window":{"context_window_size":200000,"used_percentage":12,"current_usage":{"input_tokens":1234,"output_tokens":56,"cache_creation_input_tokens":7890,"cache_read_input_tokens":50000}},"wrapper_ms":1718000000123}
```

`wrapper_ms` 用作新鲜度判定的辅助字段（> 5min 的条目视为过期丢弃），delta 计算只用本进程 `GetTickCount64`。

## 并发安全

写入端：多个 Claude Code 进程可以同时调 statusline wrapper。每个 wrapper 实例做：
```
[File]::Open(sidecar, Append, Write, Read)
$writer = New-Object StreamWriter(stream, UTF8)
$writer.WriteLine($wrapped)
$writer.Flush()
$stream.Close()
```
单 `WriteLine` + `Flush` + `Close` 在 NTFS 上对单行的写入是原子的（OS 层面行写入不超过 PIPE_BUF 等价大小）。多 writer 同时写最差情况是两行被交错成一行的半截，这种半行 JSON `nlohmann::json::parse` 会拒绝 → 跳过 → `parsed_ok=false`。

读取端：`CSidecarReader` 用 `CreateFile` 共享读模式打开 sidecar（`FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`），按 EOF 偏移增量解析，记录 `m_last_offset`，每次 `DrainNewEntries()` 只读 `m_last_offset` 到当前 EOF 之间新增的字节。

## 缺失处理

`CSidecarReader::DrainNewEntries()` 流程：
1. `CreateFile` 失败（sidecar 不存在 / 无权限）→ 返回空 list
2. `GetFileSizeEx` 返回 0 → 返回空 list
3. EOF 小于 `m_last_offset` → 视为文件被 rotate/删，重置 `m_last_offset = 0` 重新读
4. 解析失败的行 → `parsed_ok=false`，跳过，offset 仍前进
5. 所有行都成功解析 → 返回 list，每条 `parsed_ok=true`

`DataManager::Tick()` 收到空 list → 早返回，4 个柱形图保持上次值，不清零。

## 文件大小与 rotate

sidecar 一直在 append，没有截断。每次 `Tick()` 检查文件大小：
- `> 5MB` → 触发 rotate：关闭当前 handle → 把 `sidecar.jsonl` 重命名为 `sidecar.jsonl.old` → 重开 → 新文件从空开始
- `sidecar.jsonl.old`（如果存在）→ 不动，留给用户手动清

rotate 触发不影响 `m_last_offset`（offset 在重命名后立即失效，重置为 0 从新文件开始读）。rotate 那一刻正在 append 的行会随旧文件一起进 `.old`，新文件第一行会重新开始。