---
status: accepted
date: 2026-06-14
---

# 0001 — 数据源走 statusline sidecar 而非 JSONL 日志

Claude Code 的 token 数据存在两个候选来源：JSONL 会话日志与 statusline pipe 出去的实时 JSON。决策是**装一个 PowerShell wrapper 拦截 statusline、把 payload append 到 sidecar JSONL、然后由插件读 sidecar**——而不是直接 tail JSONL 文件。

JSONL 不可信：gille.ai 实测 `usage.input_tokens` 比真实值偏低 **100-174x**、`usage.output_tokens` 偏低 **10-17x**；75% 的 `input_tokens` 字段是 0 或 1 的流式占位符。Claude Code 在 stream 过程中持续更新这一字段、只在 message 收尾时落定，过程中读到的数字无法用作计费/速率数据源。

statusline JSON 的 `context_window.current_usage.{input_tokens, output_tokens, cache_creation_input_tokens, cache_read_input_tokens}` 是真实累计值，Anthropic 官方文档明示。

## Considered Options

- **A. 直读 JSONL（passive，无需装 wrapper）** — 否决。数据偏差 100x+，速度图毫无意义，数字会让用户做错决策。
- **B. statusline sidecar（需装 wrapper，1.0x 准确）** — 采纳。覆盖 CLI 与 Desktop 两种启动方式（都在 settings.json 里配 statusline）；一个 wrapper 同时拦截两者。
- **C. HTTP 抓包（mitmproxy 解密）** — 否决。订阅制用户没有 API key、proxy 解不到流量；本地 TLS 终端在 Claude Code 进程内，hook 风险高。

## Consequences

- 首次安装时必须**手动**点 Install 按钮部署 wrapper（见 ADR-0004），用户没装时任务栏全 0 是正常现象。
- 多个 Claude Code session 并行时 wrapper 会被启动多次，每次 append 一行——sidecar 是多 session 混合的 JSONL。
- sidecar 文件会**持续增长**，必须在 `Tick()` 里检测 > 5MB 时 rotate（rename `.old` + 重建）。
- wrapper 修改的是 `~/.claude/settings.json`——用户配置文件，必须 backup/restore。