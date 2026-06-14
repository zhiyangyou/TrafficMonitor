# 术语表

> 本表收录 cc-switch token 统计相关术语。来源：`src/types/usage.ts` 注释、`src-tauri/src/proxy/usage/*.rs` 注释、`src-tauri/src/services/sql_helpers.rs` 注释、`src-tauri/src/database/schema.rs` 注释。所有定义直接来自源码。

---

## A

### app_type
LLM 工具类型枚举。`database/schema.rs` 与 `src-tauri/src/app_config.rs::AppType` 定义。值：`claude` / `claude-desktop` / `codex` / `gemini` / `opencode` / `openclaw` / `hermes`。

- Dashboard 顶栏按钮只显示 `KNOWN_APP_TYPES = [claude, codex, gemini, opencode]`
- `claude-desktop` 在后端被折叠进 `claude`（详见 `folded_app_type_sql`）
- `opencode` / `openclaw` / `hermes` 没有 proxy handler，只在 managed apps 出现

### Agent（Agent）
本仓库的"Agent"概念见 `Agent`（Claude Code 工具的 subagent），与 Claude API 的 agent 无关。

### aggregateSummaries
前端 `UsageHero` 用的客户端聚合函数。把多行 `UsageSummaryByApp` 累加：
- 计数类字段直接 SUM
- `successRate` / `cacheHitRate` 从合计计数派生（不是平均）

---

## B

### backfill
回填。`backfill_missing_usage_costs` 扫描 `total_cost_usd = '0'` 且有 usage 的行，按当前 `pricing_model` 的定价重算 cost。
- 触发时机：启动时、rollup 剪枝前、定价变更后
- 按 `pricing_model` 查价，不用 `model`（避免路由接管下错算）
- 错误行（`pricing_model = ''`）和全 0 usage 行不参与

### billable_input
可计费的 input tokens。cache-inclusive 协议下 = `input - cache_read`，否则 = `input`。在 `calculator.rs:81-85` 计算。

---

## C

### cache_creation_tokens
**Anthropic 协议的 `cache_creation_input_tokens`**——把新内容写入 prompt cache 的 tokens。
- 写入时按 `pricing.cache_creation_cost_per_million` 计费（通常比 input 价贵 1.25x）
- OpenAI 协议（codex / gemini）**不区分** cache 写——这个字段对这些 app 永远是 0
- 前端对 cache-inclusive 协议显示 "N/A — 协议不上报"

### cache_read_tokens
从 prompt cache 命中的 tokens。
- Anthropic 协议：`usage.cache_read_input_tokens`，按 `cache_read_cost_per_million` 计费（通常约 input 价的 10%）
- OpenAI Codex 协议：`usage.cache_read_input_tokens` 或 `usage.input_tokens_details.cached_tokens`
- OpenAI Chat Completions 协议：`usage.prompt_tokens_details.cached_tokens`
- Gemini 协议：`usageMetadata.cachedContentTokenCount`

### cacheHitRate
缓存命中率。公式：`cache_read / (freshInput + cache_creation + cache_read)`，范围 0–1。
- 在 `usage_stats.rs::derive_real_total_and_hit_rate` 中派生
- 前端直接读 `UsageSummary.cacheHitRate`，不二次计算
- "all" 聚合时从合计计数派生，不是各行平均

### CACHE_INCLUSIVE_APP_TYPES
`input_tokens` 已包含 `cache_read_tokens` 的 app 白名单。**目前值：`["codex", "gemini"]`**。
- 后端：`sql_helpers.rs:19` 常量 + `calculator.rs:62` matches 模式
- 前端：`types/usage.ts:195-198` ReadonlySet
- 新加 provider 默认按 Claude 语义；漏加白名单会暴露为"缓存命中率过低"（比 over-deduction 易发现）

### cost_multiplier
成本倍数。影响写入时算出的 `total_cost_usd`。
- 全局默认：`proxy_config.default_cost_multiplier`（按 app_type 维度）
- 供应商级覆盖：`providers.meta.cost_multiplier`（JSON 字段）
- `claude-desktop` 的全局默认从 `claude` 继承（`logger.rs:219-223`）
- `0` 表示"不计费"（用户在 UI 显式设置）
- **只乘总价**（`base_total`），不分项乘（`calculator.rs:100`）

### Codex
OpenAI 的 CLI 工具。cc-switch 支持两种 Codex API：
- `/v1/responses`（`input_tokens` / `output_tokens`）
- `/v1/chat/completions`（`prompt_tokens` / `completion_tokens`）

`from_codex_response_auto` 自动检测用哪种。

### cumulative → delta
Codex JSONL 同步的关键算法。Codex 的 `event_msg.type=token_count` 字段是**会话级累计值**（不是单 turn 值）。`compute_delta`（`session_usage_codex.rs:107-121`）用 `saturating_sub` 还原单次 turn 的 token。
- 每次同步从 session 开头算起，跨调用之间通过 `session_log_sync.last_modified` 跳过未变文件
- 错误地 reset 累计值会导致同一 turn 被算两次

### currentModel
session 同步中的状态字段，记录当前 turn 使用的模型（从 `turn_context` 事件读）。`session_usage_codex.rs::FileParseState.current_model`。

---

## D

### data_source
`proxy_request_logs.data_source` 列。标识一行日志的来源：

| 取值 | 含义 | provider_id 形式 |
|---|---|---|
| `proxy` | 代理拦截 | 真实 provider id |
| `session_log` | Claude Code JSONL | `_session` |
| `codex_session` | Codex JSONL | `_codex_session` |
| `gemini_session` | Gemini JSON | `_gemini_session` |
| `opencode_session` | OpenCode SQLite | `_opencode_session` |

### default_cost_multiplier
全局默认成本倍数。`proxy_config` 表按 `app_type` 维度的 3 行（claude / codex / gemini）。`claude-desktop` 继承 `claude`。

### dedup_request_id
`TokenUsage::dedup_request_id()`。跨源去重的命名约定：
- 有 `message_id`：`session:{message_id}`（与 session 日志共享同一 id 空间）
- 没有：`uuid::new_v4()`（避免空 usage 行的去重把每笔请求视为同一行）

---

## E

### effective_filter / effective_usage_log_filter
聚合查询与 rollup 中使用的公共 SQL 过滤片段。排除：
- 跨源去重中被跳过的 session 行（`should_skip_session_insert`）
- 错误行（`status_code >= 400`）
- 全 0 token 的"空 usage"行（上游省略 usage 时合成）

### effective_model_sql
聚合查询中 "有效模型" 的 SQL 表达式：`COALESCE(NULLIF(pricing_model, ''), model)`。取写入时实际计价的模型作为分组键。

---

## F

### folded_app_type_sql
聚合查询的 `app_type` 折叠表达式。把 `claude-desktop` 折进 `claude`，但保留原始 `app_type` 在 detail 面板展示。Dashboard 的"all"聚合通过这个 SQL 实现。

### fresh_input / freshInput / billable_input
"实际新输入的 tokens"——cache 命中已经被分开计费，不应重复算。
- Anthropic 协议：`input_tokens` 已经是 fresh_input
- OpenAI 协议（codex / gemini）：`input_tokens` 包含 cache_read，需 `input - cache_read`
- 后端：`sql_helpers.rs::fresh_input_sql` 在 SQL 中归一
- 前端：`types/usage.ts::getFreshInputTokens` 在展示时归一

---

## G

### Gemini
Google 的 AI。cc-switch 的 Gemini 协议特点：
- `usageMetadata.promptTokenCount` / `totalTokenCount` / `cachedContentTokenCount`
- `output_tokens = totalTokenCount - promptTokenCount`（**包含 `thoughtsTokenCount`**，不要用 `candidatesTokenCount`）
- 没有 `cache_creation` 概念（不区分 cache 写）
- 走 `cache_inclusive` 语义（input 包含 cache_read）

---

## H

### has_billable_tokens
`TokenUsage::has_billable_tokens()` 判定是否产生任一计费维度 token。
- 全 0 usage 可能是真实的（OpenAI 兼容流式省略 usage 时的合成事件）
- 闸门在 handlers 层（`if let Some` 检查 usage 字段存在 + `has_billable_tokens` 检查有效）
- 防止空行虚增 `request_count`

### HEAD request
cc-switch 的"使用统计"页面对应的英文名是 "Usage"，中文是"使用量" / "用量"。注意：和 HTTP HEAD method 无关。

---

## I

### input_tokens
输入 tokens。**这是 cc-switch 中最容易引发 bug 的字段**——不同协议的语义不一致：
- Anthropic 协议：fresh input（不含 cache_read）
- OpenAI Codex 协议：包含 cache_read
- OpenAI Chat Completions 协议：包含 cache_read（`prompt_tokens` 字段）
- Gemini 协议：包含 cache_read（`promptTokenCount` 字段）

通过 `CACHE_INCLUSIVE_APP_TYPES` 区分处理。

### is_streaming
`proxy_request_logs.is_streaming` 列。是否流式响应。流式响应记录 `first_token_ms` / `duration_ms`，非流式不记录。

### isUnpricedUsage
前端 `types/usage.ts:250-264` 派生函数。识别"未计价行"：
- 2xx + 有 usage + cost = 0 + multiplier ≠ 0
- 用于显示警告徽章，等待 backfill 后消失
- multiplier = 0 不算未计价（用户主动设置）

---

## K

### KNOWN_APP_TYPES
`types/usage.ts:176-181` 常量。Dashboard 顶栏的 App 筛选按钮列表：`['claude', 'codex', 'gemini', 'opencode']`。
- 决定**显示哪些按钮**，不决定哪些行参与 "all" 聚合
- "all" 聚合的范围 = 数据库全部行（除跨源去重掉的）

---

## L

### last_modified / last_line_offset
`session_log_sync` 表的增量同步状态。`last_modified` 用 nanos 精度（`metadata_modified_nanos`），文件未变就跳过整文件。

---

## M

### message_id
`TokenUsage.message_id`。Claude API 响应中的 `id` 字段（`msg_xxx`）。用于：
1. 跨源去重（proxy 行 vs session 行共享同一 message_id）
2. `dedup_request_id` 生成 `session:{message_id}` 形式的 request_id

其他协议（OpenAI / Gemini）没有这个字段——`message_id = None`，回退到 UUID。

### model
`proxy_request_logs.model` 列。响应中实际的真实模型名（或路由接管后转发到的真实模型）。
- 与 `request_model` / `pricing_model` 可能不同（路由接管 + request 计价模式下）

### multiplier
见 `cost_multiplier`。

---

## N

### non_neg_decimal
`rust_decimal::Decimal` 的语义。cc-switch 所有 USD 金额字段都是 Decimal（不是 f64），SQLite 存储为 TEXT。查询时 `CAST(... AS REAL)` 转为 REAL，用于 SUM / GROUP BY（已知会损失精度，但 USD 数额远低于触发点）。

---

## P

### pricing_model
`proxy_request_logs.pricing_model` 列（v11 引入）。**写入时实际用于计价的模型名**。
- 路由接管下可能与 `model` 完全分叉
- `backfill` 必须按 `pricing_model` 查价
- 错误行留空字符串（`''`），历史行（v11 之前）是 NULL

### pricing_model_source
`providers.meta.pricing_model_source` + `proxy_config.pricing_model_source`。`"response"`（按响应模型计价，默认）/ `"request"`（按客户端请求模型计价）。
- 决定写入时 `pricing_model` 字段的取值
- 路由接管场景一般用 `request`（按客户端期望的模型计费）

### proxy_config
`proxy_config` 表（`schema.rs`）。三行结构：`app_type` 主键（claude / codex / gemini）。含 `proxy_enabled` / `listen_port` / 熔断器参数 / `default_cost_multiplier` / `pricing_model_source` 等。
- `claude-desktop` 没有独立行（CHECK 约束只允许 3 个值），继承 `claude`

### prompt_tokens
OpenAI Chat Completions 协议的输入 token 字段。语义同 `input_tokens`——**包含 cache_read**。

### proxy_request_logs
**核心明细表**。每条 API 请求一行。`schema.rs:186-199` 定义。当前 SCHEMA_VERSION = 11。

---

## R

### realTotalTokens
`UsageSummary.realTotalTokens`。公式：`freshInput + output + cache_creation + cache_read`（cache 归一后）。
- "实际消耗的 token"——所有计费维度求和
- 与 `totalInputTokens + totalOutputTokens` 不同——后者是 raw input/output

### request_count / success_count
聚合表 `usage_daily_rollups` 的两个计数。`success_count` 是 `status_code BETWEEN 200 AND 299` 的行数。`success_rate = success_count / request_count`。

### request_model
`proxy_request_logs.request_model` 列。**客户端请求的原始模型名**——路由接管下可能与 `model` 不同。
- v11 加入 `usage_daily_rollups` 主键，保留路由接管的"客户端别名 → 真实模型"映射维度
- 聚合表 prune 后仍可审计"哪些客户端别名走了哪些真实模型"

### rollup
聚合。`usage_daily_rollups` 表的数据来源：30 天前的 `proxy_request_logs` 被聚合成日级。
- 触发：启动时 + 周期任务
- 剪枝前必须先 backfill
- cutoff 对齐到本地午夜
- 已存在的 rollup 行用 `LEFT JOIN` 合并（幂等增量）

---

## S

### saturating_sub
Rust 的 `u32::saturating_sub`。cache > input 时返回 0 而非下溢。`calculator.rs:82` 和 `parser.rs:300` 使用。

### session_log_sync
`session_log_sync` 表。增量同步状态。`file_path` 主键，`last_modified`（nanos）+ `last_line_offset`（占位）+ `last_synced_at`。

### should_skip_session_insert
session 同步时判断是否跳过该行的函数。检查 ±10min 窗口内是否已有 `data_source='proxy'` 行。

### stream_check_logs
`stream_check_logs` 表。**与 token 统计无关**——流式连通性测试日志。不要混淆。

### success_rate
`UsageSummary.successRate`。`status_code BETWEEN 200 AND 299` 的行数 / 总行数。范围 0–1。
- "all" 聚合时从合计计数派生，不是各行平均

---

## T

### TokenUsage
Rust 核心结构（`parser.rs:16-29`）和 TS 类型（`types/usage.ts:3-8`）。四个核心字段：`input_tokens` / `output_tokens` / `cache_read_tokens` / `cache_creation_tokens`。

### total_cost_usd
`proxy_request_logs.total_cost_usd` 与 `usage_daily_rollups.total_cost_usd`。
- 明细表：`= base_total × cost_multiplier`
- 聚合表：`SUM(... AS REAL)` 累加
- TEXT 存储（Decimal），查询时 CAST

### thoughtsTokenCount
Gemini 协议特有字段。思考 token 数。**计入 `output_tokens`**（`output = totalTokenCount - promptTokenCount`），不要漏算。

---

## U

### unknown model
没有定价的模型。`CostCalculator::try_calculate_for_app` 返回 `None`，写入 `cost = None`，所有 `*_cost_usd` 留 `'0'`。`logger.rs:331-333` warn（`[USG-002]` 错误码）。`backfill` 会在有定价后补算。

### usage_daily_rollups
**核心聚合表**。`schema.rs:264-283` 定义。`SCHEMA_VERSION = 6` 引入，`v11` 扩展主键。30 天前的明细被聚合并删除。

### usage-log-recorded
Tauri 事件名。后端 `INSERT proxy_request_logs` 成功后 200ms 防抖 emit。前端 `useUsageEventBridge` 监听并 invalidate 所有 usage query。

### UsageSummary / UsageSummaryByApp / DailyStats / ProviderStats / ModelStats
后端 `usage_stats.rs:17-155` 定义的核心响应结构。前端 `types/usage.ts:68-114` 对应。

---

## V

### v11 升级
`SCHEMA_VERSION = 11` 的关键变更：`usage_daily_rollups` 重建主键，加 `request_model` / `pricing_model` 维度。SQLite 改主键需要重建表（`schema.rs:1222-1271`）。

---

## 关键常量速查

| 常量 | 值 | 位置 |
|---|---|---|
| `CACHE_INCLUSIVE_APP_TYPES` | `["codex", "gemini"]` | `sql_helpers.rs:19` / `types/usage.ts:195` |
| `KNOWN_APP_TYPES` | `["claude", "codex", "gemini", "opencode"]` | `types/usage.ts:176` |
| `SCHEMA_VERSION` | `11` | `database/mod.rs:52` |
| 默认 listen port | `15721` | `schema.rs:314` |
| 默认 listen address | `127.0.0.1` | `schema.rs:312` |
| 默认 proxy_enabled | `0`（关） | `schema.rs:306` |
| 默认 cost_multiplier | `1.0` | `schema.rs:197` |
| 默认 `first_byte_timeout` | `60s` | `schema.rs:324` |
| 默认 `idle_timeout` | `120s` | `schema.rs:328` |
| 默认 `non_streaming_timeout` | `600s` | `schema.rs:332` |
| Rollup 保留天数 | `30` | `database/mod.rs:148` / `lib.rs:1071` |
| Session 同步周期 | `60s` | `lib.rs:1071` |
| 跨源去重窗口 | `±10min` (`600s`) | `usage_stats.rs` |
| 事件防抖 | `200ms` | `usage_events.rs` |
| `PRICING_SOURCE_RESPONSE` | `"response"` | `database/mod.rs` |
| `PRICING_SOURCE_REQUEST` | `"request"` | `database/mod.rs` |
| `SESSION_REQUEST_ID_PREFIX` | `"session:"` | `parser.rs:13` |
