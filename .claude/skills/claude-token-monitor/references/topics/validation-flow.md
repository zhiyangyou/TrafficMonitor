# 验证环节 · JSONL 数据源诊断

> 2026-06-14：用户报告"重启了 TrafficMonitor 还是没数据"时做的端到端诊断。结论：**Claude Code 交互模式 statusline 命令在本环境未被触发，但 JSONL 里有真实 token 数据**。

## 验证流程

### Step 1：检查 wrapper 链路
```bash
# sidecar mtime + 大小
ls -la "C:/Users/YOU/AppData/Roaming/ClaudeTokenMonitor/sidecar.jsonl"
# 1453 字节 (5 行手动 echo) — 还是没新数据
```

### Step 2：调用 claude CLI 触发一次 assistant turn
```bash
"C:/Users/YOU/AppData/Roaming/npm/claude.cmd" -p "Reply with a single word: ok"
# 5 秒内回复 "ok"，生成项目 jsonl 但 sidecar 没新数据
```

### Step 3：检查 claude -p 生成的项目 jsonl
```bash
ls -t "C:/Users/YOU/.claude/projects/C--Users-YOU/"*.jsonl | head -1
# 最新 jsonl 18100 字节
```

jsonl 内含 `type=assistant` 真实行：
```json
{
  "type": "assistant",
  "message": {
    "model": "MiniMax-M3",
    "usage": {
      "input_tokens": 21925,
      "output_tokens": 57,
      "cache_creation_input_tokens": 0,
      "cache_read_input_tokens": 626,
      ...
    }
  }
}
```

### Step 4：检查 subagent jsonl
```bash
SUB=$(find "C:/Users/YOU/.claude/projects/" -path "*/subagents/*.jsonl" | head -1)
cat "$SUB"  # 53 个 assistant 条目，163KB
```

subagent jsonl 数据丰富，包含真实 token 数据 + 大量 streaming duplicates（同 msg_id 重复 2-5 次）。

## 关键发现

### 1. `--print` 模式不调 statusline
- claude CLI `-p/--print` 是非交互模式
- **非交互模式不调 statusLine 命令**（wrapper 永远不被调）
- 必须用交互模式（`claude` 不带 `-p`）才能触发 statusline

### 2. 沙箱里没法跑交互模式
- 交互模式需要 TUI（终端 UI）
- Bash 沙箱里 TUI 跑不动
- 没法在沙箱里验证 wrapper 链路完整调通

### 3. JSONL 数据是真实可信的
| 来源 | 数据质量 | 备注 |
|---|---|---|
| **subagent jsonl** (`~/.claude/projects/*/subagents/*.jsonl`) | ✅ 真实 | 53 个 assistant 条目，cache_read 25132-45998, input 0-21534 (含 0/1 占位符) |
| **main jsonl** (`~/.claude/projects/*/*.jsonl`) | ✅ 真实 | 3 个 assistant 条目，in=21925, 58 (全准确) |
| **sidecar.jsonl** (wrapper 写) | ❌ 不被调 | 只在交互模式触发，--print 不调，沙箱里无法触发 |

### 4. gille.ai 偏差警告的边界
- 文章说 "input_tokens 偏低 100-174x" — 那是**流式中间状态**
- **assistant turn 完成后的最终行**：`input_tokens` 是真实值
- subagent jsonl 的 `in=0` 行是 streaming 中间状态；同 msg_id 后续的 `in=806/258/4265` 是真实值
- **算法**：按 msg_id 取**最后**一条（即 streaming 完成的最终值）

## 用户决策（grill 结论）

> "JSONL 都能拿到准确数据（推荐）" + "应该读取全部的 jsonl，不要遗漏 subAgent"

**新数据源策略**（待实施）：
- 取消 statusline wrapper 作为数据源（删掉 wrapper 装/卸流程、删 `~/.claude/settings.json` 的 statusLine 注入）
- 改读全部 JSONL：
  - `~/.claude/projects/*/*.jsonl` （CLI 主项目）
  - `~/.claude/projects/*/subagents/*.jsonl` （subagent）
  - `%APPDATA%/Claude/local-agent-mode-sessions/**/projects/<dir>/*.jsonl` （Desktop agent mode，future）
- 关键算法：
  - 按 `message.id` 去重（streaming duplicates）
  - 取每个 msg_id 的**最后一条**（最终值）
  - 4 类 token 字段：`message.usage.{input_tokens, output_tokens, cache_creation_input_tokens, cache_read_input_tokens}`
  - session_id 取自 `cwd` + 文件名（jsonl 顶级无 session_id，需要派生）
  - 多 session 聚合 = 当前 plan §3 AggregateMode 逻辑

## 端到端 JSONL 真实数据样本

```json
// 来自 subagent/agent-ac05fbdaf59017d90.jsonl
{"type":"assistant","message":{"model":"MiniMax-M3","usage":{
  "input_tokens":21534,"output_tokens":132,
  "cache_creation_input_tokens":0,"cache_read_input_tokens":114,
  "server_tool_use":{"web_search_requests":0,"web_fetch_requests":0},
  "service_tier":"standard",
  "cache_creation":{"ephemeral_1h_input_tokens":0,"ephemeral_5m_input_tokens":0},
  "inference_geo":"","iterations":[],"speed":"standard"
}}}
```

## 沉淀到 skill 的变更

- 新建 `references/topics/validation-flow.md`（本文件）记录验证流程
- **待办**：在 plan 增加"切换数据源到 JSONL" 章节
- **待办**：写 ADR 0005 决策变更（原 ADR 0001 statusline-sidecar → JSONL 切换）
- **待办**：写新 topic `references/topics/jsonl-data-source.md` 描述新数据源
- **待办**：删除 CStatuslineInstaller 装/卸流程（保留路径 helper 给将来可能用）
- **待办**：重写 CSidecarReader 为 CJsonlDirectoryWatcher，扫 3 个 JSONL 路径
- **待办**：任务栏柱形图测试 — 用 claude -p 生成真实数据流
