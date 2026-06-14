---
name: cc-switch-token-stats
description: cc-switch 项目 token 统计子系统的实现原理知识库。覆盖 Tauri 本地代理拦截 (proxy_request_logs)、5 种 API 协议响应解析 (Claude / OpenAI Codex / OpenAI Chat Completions / Gemini)、成本计算 (rust_decimal 高精度)、cache-inclusive 语义陷阱 (OpenAI 协议 input_tokens 包含 cache_read)、SQLite 明细表 + usage_daily_rollups 日聚合双层结构、跨源去重 (proxy vs session_log)、Backfill 不可逆原则、定价倍率 (cost_multiplier)、4 个 session 同步源 (Claude Code JSONL / Codex JSONL / Gemini JSON / OpenCode SQLite)、前端 React Query + 实时事件桥 (usage-log-recorded)、Dashboard 顶栏筛选语义。回答关于 cc-switch token 怎么记、成本怎么算、明细怎么聚合、30 天前数据去哪了、缓存命中率怎么算、跨源去重怎么做 等问题时使用。
metadata:
  trigger:
    - "cc-switch"
    - "cc-switch token"
    - "cc-switch 用量"
    - "cc-switch 统计"
    - "proxy_request_logs"
    - "usage_daily_rollups"
    - "TokenUsage"
    - "CostCalculator"
    - "CACHE_INCLUSIVE_APP_TYPES"
    - "fresh_input"
    - "cacheHitRate"
    - "realTotalTokens"
    - "rollup_and_prune"
    - "backfill_missing_usage_costs"
    - "effective_usage_log_filter"
    - "usage-log-recorded"
    - "定价倍率"
    - "cost_multiplier"
    - "pricing_model"
    - "UsageHero"
    - "UsageTrendChart"
    - "cc-switch 计费"
    - "cc-switch 成本"
---

# cc-switch Token 统计实现原理

> 本文档是对 `G:\_GitSpace\_GitHub\cc-switch`（v3.16+）中 **token 统计子系统** 的实现原理记录。所有结论直接来自源码 + 3 个 SubAgent 并行探索 + 关键文件手读核验。引用代码位置使用 `文件路径:行号` 形式以便跳转。

---

## 1. 定位与职责

cc-switch 是一个 Tauri 2 + React 桌面应用，统一管理 Claude Code / Codex / Gemini CLI / OpenCode / OpenClaw / Hermes 等 7 个 AI CLI 工具。它的核心设计之一是 **本地代理服务器**（`127.0.0.1:15721` 默认），所有 LLM 请求都流经这个代理。

**token 统计是代理的天然产物**：
- 代理拦截请求 → 拿到上游响应 → 从响应的 `usage` 字段解析 token 数 → 写入 SQLite → 前端 Dashboard 展示。
- 同时还存在"会话日志增量同步"通道（Claude Code JSONL / Codex JSONL / Gemini JSON / OpenCode SQLite），用于补充那些没走代理的历史数据。

**核心结论（一句话）**：
> cc-switch 的 token 统计 = **双数据源写入同一张 `proxy_request_logs` 明细表**，30 天前的明细被聚合成 `usage_daily_rollups` 日报表，查询时按时间范围合并两层。成本按 `app_type` 区分 `input_tokens` 是否已含缓存读（即 OpenAI 协议的 cache-inclusive 语义），用 `rust_decimal` 高精度算价。

---

## 2. 关键文件清单（按重要性）

### ⭐ 第一梯队：核心数据流
| 路径 | 行数 | 作用 |
|---|---:|---|
| `src-tauri/src/proxy/usage/parser.rs` | 1148 | 从 5 种 API 协议响应中解析 `TokenUsage` |
| `src-tauri/src/proxy/usage/calculator.rs` | 271 | 用 `rust_decimal` 按 `ModelPricing` 计算 `CostBreakdown`，处理 cache 语义 |
| `src-tauri/src/proxy/usage/logger.rs` | 455 | 写入 `proxy_request_logs` 表 + 通知前端事件 |
| `src-tauri/src/services/usage_stats.rs` | 3927 | **核心聚合查询**：summary / trends / provider / model / 分页日志 |
| `src-tauri/src/database/dao/usage_rollup.rs` | 533 | 日聚合 + 剪枝（旧明细 → 日报表 + 删除） |
| `src-tauri/src/database/schema.rs` | — | `proxy_request_logs` / `usage_daily_rollups` / `model_pricing` 表结构与 v0→v11 迁移 |
| `src-tauri/src/commands/usage.rs` | — | 12+ 个 `#[tauri::command]` 入口 |
| `src-tauri/src/services/sql_helpers.rs` | 134 | `fresh_input_sql` —— 关键 SQL 助手 |
| `src-tauri/src/services/session_usage.rs` | 798 | Claude Code JSONL 增量同步 |
| `src-tauri/src/services/session_usage_codex.rs` | 793 | Codex JSONL 增量同步（含累计值→delta 计算） |
| `src-tauri/src/services/session_usage_gemini.rs` | 498 | Gemini JSON 同步 |
| `src/types/usage.ts` | 271 | 前端类型 + `getFreshInputTokens` / `isUnpricedUsage` |

### 第二梯队
- `src-tauri/src/database/mod.rs`（SCHEMA_VERSION = 11，启动时调 `rollup_and_prune(30)`）
- `src-tauri/src/usage_events.rs`（`usage-log-recorded` 事件 200ms 防抖）
- `src-tauri/src/lib.rs:1071`（session 同步周期任务，每 60s）
- `src-tauri/src/lib.rs:1341-1354`（usage commands 注册）
- `src/lib/api/usage.ts`（前端 `invoke("get_usage_*")` 包装）
- `src/lib/query/usage.ts`（React Query hooks）
- `src/components/usage/UsageDashboard.tsx`（总入口）+ `UsageHero.tsx` / `UsageTrendChart.tsx` / `RequestLogTable.tsx`
- `src/hooks/useUsageEventBridge.ts`（实时刷新桥）

---

## 3. 数据流全图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  通道 A：Proxy 实时拦截                                                      │
│                                                                             │
│  Client → Tauri Proxy (handlers.rs / forwarder.rs)                          │
│      → response_handler.rs → parser.rs (TokenUsage::from_*)                 │
│      → calculator.rs (CostCalculator::calculate_for_app)                   │
│      → logger.rs (UsageLogger::log_with_calculation)                        │
│      → INSERT proxy_request_logs (data_source = 'proxy')                    │
│      → usage_events::notify_log_recorded()  [200ms 防抖]                     │
└─────────────────────────────────────────────────────────────────────────────┘
                ┌──────────────────────────────────────────┐
                ↓                                          ↓
┌──────────────────────────────────┐   ┌──────────────────────────────────┐
│  通道 B：Session 日志增量同步      │   │  Rollup & Prune（启动 + 周期）    │
│  （启动一次 + 每 60s）            │   │  Database::rollup_and_prune(30)  │
│                                  │   │                                  │
│  sync_session_usage              │   │  1. backfill_missing_usage_costs │
│    ├─ Claude Code JSONL           │   │  2. INSERT OR REPLACE rollups    │
│    │  ~/.claude/projects/*/*.jsonl│   │  3. DELETE proxy_request_logs    │
│    │  + subagents + wf_*          │   │     WHERE created_at < cutoff    │
│    │  去重: message.id, 仅 assistant│   │  4. notify_log_recorded()         │
│    ├─ Codex JSONL                 │   │                                  │
│    │  ~/.codex/sessions/.../...jsonl│   │  cutoff 对齐到「下一天」本地午夜  │
│    │  关键: 累计 total_token_usage  │   │  保证被 prune 的日期是完整本地日   │
│    │       → delta = current - prev │   │                                  │
│    ├─ Gemini JSON                  │   │                                  │
│    │  ~/.gemini/tmp/*/chats/        │   │                                  │
│    │  session-*.json                │   │                                  │
│    │  output = output + thoughts   │   │                                  │
│    └─ OpenCode SQLite              │   │                                  │
│       ~/.local/share/opencode/     │   │                                  │
│       opencode.db (WAL)            │   │                                  │
│                                  │   │                                  │
│  全部 → INSERT proxy_request_logs │   │                                  │
│  provider_id 设为 _session /       │   │                                  │
│  _codex_session / _gemini_session /│   │                                  │
│  _opencode_session                │   │                                  │
│                                  │   │                                  │
│  跨源去重: ±10min 内的 proxy 行    │   │                                  │
│  优先，session 行跳过              │   │                                  │
└──────────────────────────────────┘   └──────────────────────────────────┘
                                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│  查询路径（UI 调用）                                                          │
│                                                                             │
│  Tauri Command → usage_stats::get_*_stats                                  │
│    ├─ detail 部分: SELECT FROM proxy_request_logs + effective_filter        │
│    └─ rollup 部分: SELECT FROM usage_daily_rollups + rollup_date_bounds     │
│    → UNION ALL（跨明细/汇总边界时合并）                                      │
│    → 内存中 derive_real_total_and_hit_rate                                  │
│    → 返回 UsageSummary / DailyStats / ProviderStats / ModelStats           │
│                                                                             │
│  前端: invoke("get_usage_summary", {startDate, endDate, appType, ...})      │
│     → useUsageSummaryByApp() / useUsageTrends() / ...                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. 关键概念（必懂）

### 4.1 `data_source` —— 数据来源标识

`proxy_request_logs.data_source` 列说明一行日志的来源（`database/schema.rs:198`）：

| 取值 | 含义 | provider_id 形式 |
|---|---|---|
| `proxy` | 代理拦截 | 真实 provider id |
| `session_log` | Claude Code JSONL | `_session` |
| `codex_session` | Codex JSONL | `_codex_session` |
| `gemini_session` | Gemini JSON | `_gemini_session` |
| `opencode_session` | OpenCode SQLite | `_opencode_session` |

**跨源去重** 在 `usage_stats.rs` 的 `effective_usage_log_filter` 与 `should_skip_session_insert` 中实现：同一时间窗（±10 分钟）内若已有 `data_source='proxy'` 行，session 行就被跳过——避免双算。

### 4.2 `pricing_model` vs `request_model` vs `model`（v11 引入）

`proxy_request_logs` 与 `usage_daily_rollups` 都有这三个模型相关列：

| 列 | 含义 | 示例 |
|---|---|---|
| `model` | 路由/响应中实际的真实模型 | `kimi-k2` |
| `request_model` | **客户端请求中的原始模型名（路由接管下与 model 不同）** | `claude-sonnet-4-6` |
| `pricing_model` | **写入时实际用于计价的模型名** | `kimi-k2` 或 `claude-sonnet-4-6` |

`pricing_model` 之所以重要：**缺价行补价时必须按写入时的计价基准重算**，不能用 `model` / `request_model` 猜——路由接管下三者可能各不相同。错误行（未计价）留空字符串（v11 之前的历史行是 NULL）。

### 4.3 `effective_usage_log_filter` —— 聚合查询的过滤基线

在所有 `get_*_stats` 聚合查询和 `rollup_and_prune` 中使用的公共 SQL 片段。它排除：
- 跨源去重中被跳过的 session 行（即同一请求同时被 proxy 和 session 抓到）
- 错误行（`status_code >= 400`）
- 全 0 token 的"空 usage"行（上游省略 usage 时转换器合成的全 0 终止事件）

这是 `request_count` 不虚增的关键。

### 4.4 cache 语义的核心陷阱

**OpenAI 协议（codex / gemini）的 `input_tokens` 包含缓存命中部分**，Anthropic 协议（claude）的 `input_tokens` 已经是 fresh input。

- 后端成本计算：`CostCalculator::calculate_for_app("codex", ...)` 会把 `billable_input = input - cache_read`（`calculator.rs:62-69`）
- 后端聚合查询：`fresh_input_sql(alias)`（`sql_helpers.rs:31-47`）生成 `CASE WHEN app_type IN ('codex', 'gemini') THEN input - cache_read ELSE input END` 表达式
- 前端展示：`getFreshInputTokens()`（`types/usage.ts:212-220`）做相同处理

**白名单常量**（`sql_helpers.rs:19` / `types/usage.ts:195-198`）：
```rust
const CACHE_INCLUSIVE_APP_TYPES: &[&str] = &["codex", "gemini"];
export const CACHE_INCLUSIVE_APP_TYPES = new Set(["codex", "gemini"]);
```

**新 provider 的默认**：新加 provider 默认按 Claude 语义（"input 已排除 cache"）。反向错误（OpenAI 风格却没加进白名单）会暴露为"缓存命中率过低"——比"silent over-deduction"（白名单多加了）更容易被发现。

### 4.5 `realTotalTokens` 与 `cacheHitRate`

```typescript
// 前端展示口径
realTotalTokens = freshInput + output + cache_creation + cache_read
                // (freshInput 对 cache-inclusive 协议 = input - cache_read)
cacheHitRate = cacheRead / (freshInput + cache_creation + cacheRead)
            // 范围 0-1
```

这两个字段的派生都在 `usage_stats.rs` 的 `derive_real_total_and_hit_rate` 中，前端 React Query hook 直接拿到 `UsageSummary` 后不再二次计算（除了 `UsageHero` 的 "all" 聚合时客户端累加）。

### 4.6 剪枝的"不可逆原则"

`usage_daily_rollups` 是**按本地日聚合**的，剪枝后明细行**永远**丢失 token 与 cost 细节。因此：

```rust
// usage_rollup.rs:78-85
// 剪枝是不可逆的：明细一旦汇总删除，0 成本行就永远失去按 pricing_model
// 补价重算的机会。所以剪枝前先尽力回填一次。失败仅告警不阻断。
if let Err(e) = Self::backfill_missing_usage_costs_on_conn(&conn, None) {
    log::warn!("Pre-prune cost backfill failed, pruning anyway: {e}");
}
```

`backfill_missing_usage_costs` 在 `usage_stats.rs` 末尾：扫描所有 `total_cost_usd = 0` 且有 usage 的行，按当前 `pricing_model` 的定价重算 cost。**这就是为什么保留 `pricing_model` 列很重要**——回填必须按写入时的计价基准。

### 4.7 Cutoff 对齐到本地午夜

```rust
// usage_rollup.rs:19-55 compute_local_midnight_cutoff
// 目标日 = now - retain_days
// cutoff = (目标日 + 1 天)的本地午夜 00:00:00
```

为什么：保留明细 30 天 = 保留到 `now - 30天` 之**前**的完整日。
- 如果 `now = 2026-04-16 14:32`、`retain_days = 30`，目标日 = `2026-03-17`，cutoff = `2026-03-18 00:00 local`。
- 这样保留的是 `2026-03-18 00:00` 到 `2026-04-16 14:32` 之间的所有数据（29 天零 14 小时），剪掉的是 `2026-03-18 00:00` 之前的所有数据。
- 被剪的都是"完整本地日"——`compute_rollup_date_bounds` 配对检查时也只纳入完全被查询范围覆盖的 rollup 日期。

DST 边界处理：本地午夜若落在 DST gap 中，回退到 +1 小时；如果仍然不存在就报错。

### 4.8 Provider 倍率 vs 全局倍率

`UsageLogger::resolve_pricing_config` 决定 `cost_multiplier`（`logger.rs:211-303`）：

1. 查 `proxy_config` 表的 `default_cost_multiplier`（按 `app_type` 维度，行式结构有 3 行：claude / codex / gemini）
2. `claude-desktop` app 没有独立配置，回退继承 `claude`
3. 供应商 `meta` JSON 里的 `cost_multiplier` 覆盖全局默认
4. `pricing_model_source` 同理：`response`（按响应中的真实模型计价）/ `request`（按客户端请求模型计价）二选一

`calculate_with_cache_semantics`（`calculator.rs:71-109`）**只在 `total_cost` 阶段乘 `cost_multiplier`**，各分项成本是"基础价"。

---

## 5. 解析层细节

### 5.1 五种协议解析

`parser.rs` 提供 `from_*_response` 与 `from_*_stream_events` 系列方法：

| 协议 | 非流式方法 | 流式方法 | 关键字段 |
|---|---|---|---|
| Claude | `from_claude_response` | `from_claude_stream_events` | `usage.input_tokens / output_tokens / cache_read_input_tokens / cache_creation_input_tokens` |
| OpenRouter | `from_openrouter_response` | — | `usage.prompt_tokens / completion_tokens`（cache 字段 = 0） |
| Codex (OpenAI Chat) | `from_openai_response` | `from_openai_stream_events` | `usage.prompt_tokens / completion_tokens / prompt_tokens_details.cached_tokens` |
| Codex (Responses) | `from_codex_response` | `from_codex_stream_events_auto` | `usage.input_tokens / output_tokens / cache_read_input_tokens` 或 `input_tokens_details.cached_tokens` |
| Codex (auto) | `from_codex_response_auto` | `from_codex_stream_events_auto` | 自动判别用上面哪个 |
| Gemini | `from_gemini_response` | `from_gemini_stream_chunks` | `usageMetadata.promptTokenCount / totalTokenCount / cachedContentTokenCount`；**output = total - prompt**（含 thoughtsTokenCount） |

### 5.2 Claude 流式响应的"delta input 修正"语义

`from_claude_stream_events`（`parser.rs:96-211`）处理三种事件：`message_start` / `message_delta` / 其他。

**关键洞察**：部分 Anthropic-compatible provider（Qwen / 智谱等）会在 `message_start` 上报包含缓存的**总上下文**，但在 `message_delta` 上报**修正后的 fresh input**。需要以 `message_delta` 的 input 为准（更小、更准确）。

判断条件（`parser.rs:163-179`）：
```
should_use_delta_input =
    delta_input > 0
    && (
        start_input == 0                // 起始没数
        || delta_input < start_input    // delta 更小（修正）
        || (input_from_delta && delta_input <= start_input)  // 之前已采纳过 delta
    )
```

`input_from_delta` 标志确保**一旦采纳过 delta，后续相同/更小的 delta 继续覆盖缓存字段**（保持三个字段同源）。

### 5.3 Codex 的累计值→delta 计算

Codex JSONL 里的 `event_msg.type=token_count` 是**会话级累计值**（`session_usage_codex.rs:107-121` `compute_delta`）：
```rust
input: current.input.saturating_sub(p.input)
cached_input: current.cached_input.saturating_sub(p.cached_input)
output: current.output.saturating_sub(p.output)
```

每次同步（启动 + 每 60s）扫描整个文件，跨调用之间通过 `session_log_sync.last_modified` 跳过未变文件，**单次扫描内**按出现顺序对相同 `event_msg` 序列做 `delta` 运算。这样就能从累计值还原出单次 turn 的 token。

### 5.4 `has_billable_tokens` 闸门

`TokenUsage::has_billable_tokens()`（`parser.rs:46-51`）判断是否产生任一计费维度 token。
**全 0 usage 也可能真实存在**（如 OpenAI 兼容上游流式下省略 usage），但**全 0 usage 又有 message_id** 就要保留（用于跨源去重）；**全 0 usage 又没 message_id** 必须丢弃——否则 `dedup_request_id` 退化为随机 UUID，每笔请求插入一条无意义的空行、虚增请求数。

更精细的过滤发生在 handlers 层（在 `proxy/handlers.rs` 等）：先用 `if let Some` 检查 usage 字段存在，再用 `has_billable_tokens` 闸门。

---

## 6. 计算与定价

### 6.1 `ModelPricing` 与 `CostBreakdown`

```rust
// calculator.rs:11-26
pub struct CostBreakdown {
    pub input_cost: Decimal,
    pub output_cost: Decimal,
    pub cache_read_cost: Decimal,
    pub cache_creation_cost: Decimal,
    pub total_cost: Decimal,
}

pub struct ModelPricing {
    pub input_cost_per_million: Decimal,
    pub output_cost_per_million: Decimal,
    pub cache_read_cost_per_million: Decimal,
    pub cache_creation_cost_per_million: Decimal,
}
```

所有金额字段是 `rust_decimal::Decimal`（精度任意），**存入 SQLite 用 TEXT**。聚合查询时 `CAST(... AS REAL)` 转为 REAL（已知会损失精度，但用于 group by / 求和够用）。

### 6.2 `calculate_for_app` 的 cache 语义分支

```rust
// calculator.rs:56-69
let input_includes_cache_read = matches!(app_type, "codex" | "gemini");
Self::calculate_with_cache_semantics(usage, pricing, cost_multiplier, input_includes_cache_read)
```

`calculate_with_cache_semantics`（`calculator.rs:71-109`）：
```rust
let billable_input = if input_includes_cache_read {
    usage.input_tokens.saturating_sub(usage.cache_read_tokens)
} else {
    usage.input_tokens
};

let input_cost = Decimal::from(billable_input) * pricing.input_cost_per_million / Decimal::from(1_000_000);
let output_cost = Decimal::from(usage.output_tokens) * pricing.output_cost_per_million / 1_000_000;
let cache_read_cost = Decimal::from(usage.cache_read_tokens) * pricing.cache_read_cost_per_million / 1_000_000;
let cache_creation_cost = Decimal::from(usage.cache_creation_tokens) * pricing.cache_creation_cost_per_million / 1_000_000;

let base_total = input_cost + output_cost + cache_read_cost + cache_creation_cost;
let total_cost = base_total * cost_multiplier;  // 倍率只作用于总价
```

**关键点**：cost_multiplier 只乘总价，不乘各分项。所以 `input_cost_usd + output_cost_usd + cache_*_cost_usd == base_total != total_cost_usd`（当 multiplier ≠ 1 时）。

### 6.3 定价查找链

`find_model_pricing_row`（`usage_stats.rs:1943-1967`）三级匹配：

1. 精确匹配 `model_id`
2. 候选归一化：剥 `openai./anthropic./google./bedrock.` namespace、剥 `-v\d+`、剥 `-YYYY-MM-DD`/`-YYYYMMDD` 日期、剥 `-minimal/-low/-medium/-high/-xhigh` 变体后缀
3. 前缀匹配（仅 `claude-` / `o1/o3/o4/o5` / `gpt-` / `gemini-` / `deepseek-` 等家族）

启动时调用 `ensure_model_pricing_seeded`（`schema.rs:1276-2104` 的 `seed_model_pricing`）保证 100+ 主流模型都有默认价。

---

## 7. 持久化

### 7.1 数据库位置
- 文件：`~/.cc-switch/cc-switch.db`
- 驱动：`rusqlite 0.31`（bundled）
- 当前 `SCHEMA_VERSION = 11`（`database/mod.rs:52`）

### 7.2 核心表

```sql
-- proxy_request_logs（明细，最近 30 天）
CREATE TABLE proxy_request_logs (
    request_id TEXT PRIMARY KEY,
    provider_id TEXT NOT NULL,
    app_type TEXT NOT NULL,
    model TEXT NOT NULL,
    request_model TEXT,                          -- v11+ 用于路由接管审计
    pricing_model TEXT,                          -- v11+ 写入时计价基准
    input_tokens INTEGER NOT NULL DEFAULT 0,
    output_tokens INTEGER NOT NULL DEFAULT 0,
    cache_read_tokens INTEGER NOT NULL DEFAULT 0,
    cache_creation_tokens INTEGER NOT NULL DEFAULT 0,
    input_cost_usd TEXT NOT NULL DEFAULT '0',
    output_cost_usd TEXT NOT NULL DEFAULT '0',
    cache_read_cost_usd TEXT NOT NULL DEFAULT '0',
    cache_creation_cost_usd TEXT NOT NULL DEFAULT '0',
    total_cost_usd TEXT NOT NULL DEFAULT '0',
    latency_ms INTEGER NOT NULL,
    first_token_ms INTEGER,
    duration_ms INTEGER,
    status_code INTEGER NOT NULL,
    error_message TEXT,
    session_id TEXT,
    provider_type TEXT,
    is_streaming INTEGER NOT NULL DEFAULT 0,
    cost_multiplier TEXT NOT NULL DEFAULT '1.0',
    created_at INTEGER NOT NULL,
    data_source TEXT NOT NULL DEFAULT 'proxy'
);

-- usage_daily_rollups（聚合，明细被 prune 后保留）
CREATE TABLE usage_daily_rollups (
    date TEXT NOT NULL,                          -- 'YYYY-MM-DD' 本地
    app_type TEXT NOT NULL,
    provider_id TEXT NOT NULL,
    model TEXT NOT NULL,
    request_model TEXT NOT NULL DEFAULT '',
    pricing_model TEXT NOT NULL DEFAULT '',
    request_count INTEGER NOT NULL DEFAULT 0,
    success_count INTEGER NOT NULL DEFAULT 0,
    input_tokens INTEGER NOT NULL DEFAULT 0,
    output_tokens INTEGER NOT NULL DEFAULT 0,
    cache_read_tokens INTEGER NOT NULL DEFAULT 0,
    cache_creation_tokens INTEGER NOT NULL DEFAULT 0,
    total_cost_usd TEXT NOT NULL DEFAULT '0',
    avg_latency_ms INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (date, app_type, provider_id, model, request_model, pricing_model)
);
```

### 7.3 关键索引

- `idx_request_logs_created_at` (`created_at`) —— 时间范围查询主力
- `idx_request_logs_provider` (`provider_id`, `app_type`) —— provider 维度
- `idx_request_logs_model` (`model`) —— model 维度
- `idx_request_logs_session` (`session_id`) —— 会话关联
- `idx_request_logs_status` (`status_code`) —— 状态码过滤
- `idx_request_logs_dedup_lookup_expr`（表达式索引，跨源去重性能优化）

### 7.4 Migrations 关键点

- **v1→v2**：创建 `proxy_request_logs` + `model_pricing`
- **v5→v6**：创建 `usage_daily_rollups`
- **v7→v8**：加 `data_source` 列 + `session_log_sync` 表 + 修正 13 个模型定价
- **v8→v9**：清空重 seed 全量模型定价
- **v10→v11**：`usage_daily_rollups` 重建主键，加 `request_model` / `pricing_model` 维度（SQLite 改主键需重建表）

迁移用 SAVEPOINT 包裹，失败回滚。

---

## 8. 前端数据流

### 8.1 三层抽象

```
React Component (UsageDashboard / UsageHero / ...)
   ↓ 调 hook
React Query hook (src/lib/query/usage.ts)
   ↓ 调 api
API wrapper (src/lib/api/usage.ts)
   ↓ invoke("get_usage_*")
Tauri Command (src-tauri/src/commands/usage.rs)
   ↓ 调 service
Service (src-tauri/src/services/usage_stats.rs)
   ↓ SQL
SQLite
```

### 8.2 React Query 缓存键

`usageKeys` 包含所有筛选维度，保证按筛选条件隔离：
```typescript
usageKeys.trends(preset, customStartDate, customEndDate, { appType, providerName, model })
```

### 8.3 实时刷新机制

`src/hooks/useUsageEventBridge.ts` 监听 Tauri 事件 `usage-log-recorded`（后端在 `proxy_request_logs` 写入时 200ms 防抖 emit），收到后 `queryClient.invalidateQueries({ queryKey: usageKeys.all })`，让所有 usage 查询立刻重拉——无需等 30s 轮询。

### 8.4 Dashboard 顶栏筛选

| 维度 | 控件 | 行为 |
|---|---|---|
| App Type | 4 个带图标的 toggle 按钮 | 切换时清掉 Provider 和 Model |
| Provider | Select 动态下拉 | 选项来自 `useProviderStats` 实际有数据的 provider |
| Model | Select 动态下拉 | 随 Provider **级联** |
| 时间范围 | `UsageDateRangePicker` | 5 个预设 + 自定义（日历+时分） |
| 刷新频率 | Select(0/5s/10s/30s/60s) | 改动后立即 invalidate |
| 状态码（仅日志 Tab） | Select | 仅作用于 `RequestLogTable` |

### 8.5 "all" 聚合的特殊处理

`UsageHero` 中 `pickSummary(allApps, appType)`：
- 指定 app：直接取该行
- `all`：客户端用 `aggregateSummaries()` 累加多行；`successRate` / `cacheHitRate` 从合计计数重新派生（不是平均）

`claude-desktop` 在后端被折叠进 `claude`（`folded_app_type_sql`），但保留原始 `app_type` 用于详情面板审计。`KNOWN_APP_TYPES` 列表（`types/usage.ts:176-181`）只决定显示哪些筛选按钮，不决定哪些行参与 "all" 聚合。

---

## 9. 踩坑指南（容易踩的坑）

1. **cache 语义遗漏**：新加 OpenAI 风格的 provider 但忘了加进 `CACHE_INCLUSIVE_APP_TYPES` —— 表现是 `inputTokens` 偏大、`cacheHitRate` 偏低、`realTotalTokens` 偏大。**反方向错误（加多了非 cache-inclusive 的 provider）** 会让 `input` 被错误减掉，更隐蔽。

2. **剪枝前忘了补价**：`backfill_missing_usage_costs` 必须在 `rollup_and_prune` 之前调用，0 成本行被 prune 后就再也补不上了。

3. **cutoff 没对齐本地午夜**：如果 cutoff 落在一天的中段，那天会被半 rollup / 半 prune，下次 `compute_rollup_date_bounds` 涉及该日时会漏算。

4. **Codex delta 跨调用的累计值**：每次同步必须从 session 开始算 delta（不能跨文件 / 跨调用 reset），否则同一 turn 会被算两次。

5. **跨源去重的 10 分钟窗**：±10 分钟的容差太大可能误杀，太小可能漏掉 proxy 延迟写。Codex 的 token_count 事件和 proxy 响应可能差几秒到几分钟。

6. **GEMINI 的 thoughtsTokenCount**：Gemini 的 `output_tokens` 必须包含思考 token（`output = totalTokenCount - promptTokenCount`），不要用 `candidatesTokenCount`，否则会少算 thoughts。

7. **Anthropic 兼容 provider 的 message_delta 修正**：Qwen / 智谱等会在 `message_start` 报"包含缓存的总上下文"，在 `message_delta` 报"fresh input"。必须以 `delta` 为准，且 `input_from_delta` 标志要保持三个字段同源。

8. **`claude-desktop` app**：代理流量仍记录在 `claude-desktop` 这个 app_type 下（保留路由接管计费审计），但 Dashboard 把它折叠进 `claude` 展示。详情面板显示真实值。

9. **`claude-desktop` 没有独立的全局计费配置**：`proxy_config` 表的 CHECK 约束只允许 `claude` / `codex` / `gemini`，所以 `claude-desktop` 的全局默认必须从 `claude` 继承（`logger.rs:219-223`）。

10. **pricing_model 必须存**：路由接管 + request 计价模式下，`pricing_model` 可能与 `model` 完全分叉。如果只存 `model`，回填会按错的模型计价。错误行留空字符串（`''`）而不是 NULL（v11 之前是 NULL）——这是 v11 改的。

---

## 10. 子文档索引

深入阅读请按以下顺序查看 `references/` 下的子文档：

- `references/parser-and-calculator.md` —— 解析器与计算器的字段映射、cache 语义、5 种协议对照表
- `references/persistence.md` —— SQLite schema、rollup 算法、跨源去重、backfill 机制
- `references/frontend-ui.md` —— 前端组件、React Query 模式、实时刷新、筛选语义
- `references/glossary.md` —— 关键术语表（cache-inclusive / fresh-input / effective filter / data_source / pricing_model 等）

---

## 11. 快速定位指南

| 我想知道… | 看哪里 |
|---|---|
| 某个 app 的 input 是否包含 cache | `references/glossary.md` 的 `CACHE_INCLUSIVE_APP_TYPES` |
| 某个 API 响应怎么被解析 | `references/parser-and-calculator.md` 的协议对照表 |
| 成本怎么算的 | `proxy/usage/calculator.rs:71-109` |
| 30 天前数据去哪了 | `references/persistence.md` 的 Rollup 算法 |
| 缓存命中率怎么算 | `usage_stats.rs` 的 `derive_real_total_and_hit_rate` + `types/usage.ts:79` |
| 跨源去重怎么做 | `usage_stats.rs` 的 `effective_usage_log_filter` + `should_skip_session_insert` |
| 前端页面是怎么拼的 | `references/frontend-ui.md` |
| 数据库 schema 长啥样 | `references/persistence.md` |
| 定价默认值在哪 | `database/schema.rs:1276-2104` `seed_model_pricing()` |
