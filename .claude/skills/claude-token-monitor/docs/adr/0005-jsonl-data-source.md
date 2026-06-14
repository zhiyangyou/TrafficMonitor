---
status: accepted
date: 2026-06-14
---

# 切换数据源：statusline sidecar → JSONL 直读

## 背景

`ADR 0001` 选了 `statusline sidecar` 作为数据源：在 `~/.claude/settings.json` 注入 statusline wrapper 脚本，每次 Claude Code 完成一个 assistant turn 后调 wrapper 写一行 JSON 到 `%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl`。

## 决策

**改读 Claude Code 项目 JSONL**，废弃 statusline wrapper 链路。

## 为什么

1. **`claude -p` 不调 statusline**：Anthropic CLI 文档明确 `--print` 是非交互模式，不渲染 statusline。这意味着所有 `--print` 模式的会话、CI 脚本、subagent 跑的任务**都不会写 sidecar**。
2. **沙箱里无法验证 wrapper 链路**：交互模式需要 TUI（终端 UI），沙箱环境跑不动，没法在自动化环境里"调一次 wrapper 验证 wrapper 调一次插件"端到端通。
3. **JSONL 数据真实可信**（gille.ai 警告的边界条件）：
   - `gille.ai` 说 "input_tokens 偏低 100-174x" — 那是**流式中间状态**（streaming placeholder）。
   - **assistant turn 完成后的最终行**，`input_tokens` / `output_tokens` 是真实账单值。
   - 用户环境实测 subagent jsonl 53 个 assistant 条目，输入 21534/2480/806/258/4265 ... 等等，**都是真实消耗**而非占位符 0/1。
4. **读全部 JSONL**（用户决策）：包括 main `projects/*/*.jsonl` + `projects/*/subagents/*.jsonl`，**不遗漏 subagent**。

## Considered Options

### A. 保留 statusline sidecar（passes）
- 优点：和原 plan 一致；理论上 Claude Code 交互模式 statusline 触发是可靠的
- 否决：`-p` 模式完全不调；沙箱里没法验证；subagent 跑的任务可能不调；用户实际用 `--print` 流程时 0 数据

### B. 切换到 JSONL 直读（采纳）
- 优点：所有模式（`--print`、交互、subagent）都覆盖；不需要 wrapper 链路；沙箱可验证
- 代价：需要写一个 JSONL directory watcher（CSidecarReader → CJsonlDirectoryWatcher 重写）；需要按 `msg_id` 去重 streaming duplicates

### C. 双源合并（sidecar 优先，JSONL 兜底）
- 否决：复杂度 ×2；调试时不知道哪条数据从哪来

## 关键算法：JSONL 去重 + delta

```
for each watched .jsonl file:
    lines = tail(new entries since last read)
    for each line:
        j = parse JSON
        if j.type != "assistant": continue
        msg_id = j.message.id
        if msg_id not seen:
            seen[msg_id] = { input, output, cache_creation, cache_read, ts_ms }
        else:
            # streaming duplicate — UPDATE with later (final) values
            seen[msg_id] = { input, output, cache_creation, cache_read, ts_ms }
    for each msg_id with ts_ms > last_tick_ms:
        delta.input       = current.input  - prev.input
        delta.output      = current.output - prev.output
        delta.cache_creation = current.cache_creation - prev.cache_creation
        delta.cache_read  = current.cache_read - prev.cache_read
        push delta to 1Hz window
```

## 监控的 JSONL 路径

| 路径 | 覆盖场景 |
|---|---|
| `~/.claude/projects/*/*.jsonl` | CLI 主项目 |
| `~/.claude/projects/*/subagents/*.jsonl` | subagent（用户特别要求不漏） |
| `%APPDATA%/Claude/local-agent-mode-sessions/**/projects/<dir>/*.jsonl` | Desktop agent mode（future，v1 可暂缓） |

## Session 派生

JSONL 文件路径包含 session_id。`projects/C--Users-YOU/0ecde59e-8d90-46db-954f-2eb32ae7b8e6.jsonl` 中 UUID 形式文件名 = session_id。

v1 用 jsonl 文件 UUID 路径片段做 session_id；cwd 取自 JSONL 内的 user 行的 `cwd` 字段（同一 session 一致）。

## Consequences

1. 删 `CStatuslineInstaller::Install/Uninstall`（任务 32）— 保留 `GetXxxPath` 给将来可能用
2. 重写 `CSidecarReader` → `CJsonlDirectoryWatcher`（任务 31）
3. `COptionsDlg` 删 wrapper 装/卸按钮和相关状态行/路径详情
4. ADR 0001（statusline-sidecar）标 `superseded by ADR 0005`
5. `references/topics/data-source.md` 改为以 JSONL 为主（保留 sidecar 作为参考）

## References

- 验证流程：`references/topics/validation-flow.md`
- gille.ai 警告原文：https://gille.ai/en/blog/claude-code-jsonl-logs-undercount-tokens/
- Anthropic CLI 文档（statusline）：https://code.claude.com/docs/en/statusline
