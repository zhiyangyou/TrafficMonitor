# Parser & Calculator 详解

> 本文深入 `src-tauri/src/proxy/usage/parser.rs` 与 `src-tauri/src/proxy/usage/calculator.rs`，
> 重点说清 "5 种协议字段映射" 与 "cache 语义的代价处理"。

---

## 1. `TokenUsage` 数据结构

```rust
// parser.rs:16-29
pub struct TokenUsage {
    pub input_tokens: u32,
    pub output_tokens: u32,
    pub cache_read_tokens: u32,
    pub cache_creation_tokens: u32,
    pub model: Option<String>,             // 响应中提取的 model
    pub message_id: Option<String>,        // 用于跨源去重,Claude 才有
}

impl TokenUsage {
    pub fn has_billable_tokens(&self) -> bool { ... }   // 4 个字段任一 > 0
    pub fn dedup_request_id(&self) -> String { ... }   // message_id → "session:{id}", 否则随机 UUID
}
```

四个核心数字是所有变体归一的目标。"cache 写"（`cache_creation`）目前只有 Anthropic 协议会显式返回。

---

## 2. 协议字段对照表

| 协议 | 流式 | 非流式函数 | 字段路径 |
|---|---|---|---|
| **Claude (Anthropic)** | ✅ | `from_claude_response` | `usage.input_tokens / output_tokens / cache_read_input_tokens / cache_creation_input_tokens` |
| **Claude 流式** | — | `from_claude_stream_events` | `message.message.usage` (start) + `usage` (delta) |
| **OpenRouter** | — | `from_openrouter_response` | `usage.prompt_tokens / completion_tokens` |
| **Codex (OpenAI Chat)** | ✅ | `from_openai_response` | `usage.prompt_tokens / completion_tokens / prompt_tokens_details.cached_tokens` |
| **Codex (OpenAI Responses)** | ✅ | `from_codex_response` | `usage.input_tokens / output_tokens / cache_read_input_tokens` 或 `input_tokens_details.cached_tokens` |
| **Codex (自动检测)** | ✅ | `from_codex_response_auto` / `from_codex_stream_events_auto` | 看 `usage.prompt_tokens` 还是 `usage.input_tokens` 决定走 OpenAI 还是 Codex 路径 |
| **Gemini** | ✅ | `from_gemini_response` | `usageMetadata.promptTokenCount / totalTokenCount / cachedContentTokenCount` |

> **重要细节（Gemini）**：`output_tokens = totalTokenCount - promptTokenCount`，这**包含** `thoughtsTokenCount`（思考 token 计入 output）。

---

## 3. 关键函数实现

### 3.1 `from_claude_response`（非流式）

```rust
// parser.rs:66-92
let usage = body.get("usage")?;
let model = body.get("model").and_then(|v| v.as_str());
let message_id = body.get("id").and_then(|v| v.as_str());

Some(Self {
    input_tokens: usage.get("input_tokens")?.as_u64()? as u32,
    output_tokens: usage.get("output_tokens")?.as_u64()? as u32,
    cache_read_tokens: usage.get("cache_read_input_tokens").and_then(|v| v.as_u64()).unwrap_or(0) as u32,
    cache_creation_tokens: usage.get("cache_creation_input_tokens").and_then(|v| v.as_u64()).unwrap_or(0) as u32,
    model, message_id,
})
```

### 3.2 `from_claude_stream_events`（流式 + 修正语义）

```rust
// parser.rs:96-211
for event in events {
    if event.type == "message_start" {
        // 从 message.usage 读 input_tokens / cache_read / cache_creation
    }
    if event.type == "message_delta" {
        // 从 usage 读 output_tokens
        // 关键：处理"delta 修正 input"的语义（见下）
    }
}

if usage.has_billable_tokens() { Some(usage) } else { None }
```

**Delta 修正的判定条件**（`parser.rs:163-179`）：
```rust
let should_use_delta_input = input > 0
    && (
        usage.input_tokens == 0                       // 起始没数
        || input < usage.input_tokens                 // delta 更小（修正）
        || (input_from_delta && input <= usage.input_tokens)  // 之前已采纳过 delta
    );

if should_use_delta_input {
    usage.input_tokens = input;
    input_from_delta = true;  // 标志：之后 delta 与之比较而非 start
    if let Some(cache_read) = delta_cache_read { usage.cache_read_tokens = cache_read; }
    if let Some(cache_creation) = delta_cache_creation { usage.cache_creation_tokens = cache_creation; }
}
```

**为什么需要这个分支**：部分 Anthropic-compatible provider（Qwen / 智谱 / GLM 等）会在 `message_start` 上报**包含缓存的总上下文**，在 `message_delta` 上报**修正后的 fresh input**。两者只能信一个。优先信 delta 的更小值（修正后）。

**回归测试覆盖**（`parser.rs:903-1010`）：
- `test_claude_stream_prefers_smaller_delta_input_and_cache_pair` —— Qwen-max，start 200K→delta 80K，采纳 delta
- `test_claude_stream_updates_cache_pair_from_later_delta_input` —— 多次 delta 保持同源
- `test_claude_stream_keeps_start_when_delta_input_is_larger` —— delta 变大时不覆盖 start

### 3.3 `from_codex_response_adjusted`（Codex 调整版）

```rust
// parser.rs:282-319
let input_tokens = usage.get("input_tokens")?.as_u64()? as u32;
let output_tokens = usage.get("output_tokens")?.as_u64()? as u32;

let cached_tokens = usage.get("cache_read_input_tokens").and_then(|v| v.as_u64())
    .or_else(|| usage.get("input_tokens_details").and_then(|d| d.get("cached_tokens")).and_then(|v| v.as_u64()))
    .unwrap_or(0) as u32;

let adjusted_input = input_tokens.saturating_sub(cached_tokens);  // 防下溢

Some(Self {
    input_tokens: adjusted_input,    // 这里已经把 cache 扣掉了
    output_tokens,
    cache_read_tokens: cached_tokens,
    cache_creation_tokens: usage.get("cache_creation_input_tokens").and_then(|v| v.as_u64()).unwrap_or(0) as u32,
    ...
})
```

**注意区分**：
- `from_codex_response`（非调整版）：`input_tokens` 保留原值，**cost 计算时**才扣 cache（`calculator.rs:81-85`）
- `from_codex_response_adjusted`（调整版）：`input_tokens` 已扣 cache，cost 计算直接用

实际写入数据库时**只走非调整版**（`from_codex_response` / `from_codex_response_auto`），扣减动作在 `calculate_for_app` 统一处理。这样数据库里保留的是"原始上报值"，成本计算逻辑集中。

### 3.4 `from_gemini_response`

```rust
// parser.rs:433-459
let usage = body.get("usageMetadata")?;
let model = body.get("modelVersion").and_then(|v| v.as_str());
let prompt_tokens = usage.get("promptTokenCount")?.as_u64()? as u32;
let total_tokens = usage.get("totalTokenCount")?.as_u64()? as u32;
let output_tokens = total_tokens.saturating_sub(prompt_tokens);  // 包含 thoughts

Some(Self {
    input_tokens: prompt_tokens,
    output_tokens,  // = total - prompt = candidates + thoughts
    cache_read_tokens: usage.get("cachedContentTokenCount").and_then(|v| v.as_u64()).unwrap_or(0) as u32,
    cache_creation_tokens: 0,  // Gemini 不区分 cache 写
    model, message_id: None,
})
```

**关键陷阱**：Gemini 没有 `cache_creation_input_tokens` 概念，**`cache_creation_tokens` 永远是 0**。前端展示时这个字段对 `gemini` 应该是 N/A。

---

## 4. `CostCalculator` 详解

### 4.1 入口

```rust
// calculator.rs:44-69
pub fn calculate(usage, pricing, cost_multiplier) -> CostBreakdown {
    Self::calculate_with_cache_semantics(usage, pricing, cost_multiplier, false)
}

pub fn calculate_for_app(app_type, usage, pricing, cost_multiplier) -> CostBreakdown {
    let input_includes_cache_read = matches!(app_type, "codex" | "gemini");
    Self::calculate_with_cache_semantics(usage, pricing, cost_multiplier, input_includes_cache_read)
}
```

`calculate_for_app` 是**写入路径**唯一入口。`calculate` 是历史保留 / 测试用的简单版本（cache 语义 = false）。

### 4.2 `calculate_with_cache_semantics`

```rust
// calculator.rs:71-109
let million = Decimal::from(1_000_000);

let billable_input_tokens = if input_includes_cache_read {
    usage.input_tokens.saturating_sub(usage.cache_read_tokens)
} else {
    usage.input_tokens
};

let input_cost = Decimal::from(billable_input_tokens) * pricing.input_cost_per_million / million;
let output_cost = Decimal::from(usage.output_tokens) * pricing.output_cost_per_million / million;
let cache_read_cost = Decimal::from(usage.cache_read_tokens) * pricing.cache_read_cost_per_million / million;
let cache_creation_cost = Decimal::from(usage.cache_creation_tokens) * pricing.cache_creation_cost_per_million / million;

let base_total = input_cost + output_cost + cache_read_cost + cache_creation_cost;
let total_cost = base_total * cost_multiplier;  // 倍率只乘总价

CostBreakdown { input_cost, output_cost, cache_read_cost, cache_creation_cost, total_cost }
```

### 4.3 公式总结

| 量 | 公式 |
|---|---|
| `billable_input` | Claude: `input`; Codex/Gemini: `max(input - cache_read, 0)` |
| `input_cost` | `billable_input × input_price / 1e6` |
| `output_cost` | `output × output_price / 1e6` |
| `cache_read_cost` | `cache_read × cache_read_price / 1e6` |
| `cache_creation_cost` | `cache_creation × cache_creation_price / 1e6` |
| `base_total` | 四项之和 |
| `total_cost` | `base_total × cost_multiplier` |

### 4.4 数值类型与精度

- **全程** `rust_decimal::Decimal`，不是 `f64`。
- SQLite 存储用 `TEXT`（`schema.rs:192-194`），查询时 `CAST(... AS REAL)` 转浮点（用于 SUM / GROUP BY）。
- JS 端 `Number.parseFloat(log.totalCostUsd)` 转回 `number` 展示（已知超 1e15 损失精度，但 USD 数额远低于此）。

### 4.5 `try_calculate_for_app` 与未知模型

```rust
// calculator.rs:121-128
pub fn try_calculate_for_app(app_type, usage, pricing: Option<&ModelPricing>, cost_multiplier) -> Option<CostBreakdown> {
    pricing.map(|p| Self::calculate_for_app(app_type, usage, p, cost_multiplier))
}
```

**`pricing` 为 None 时返回 None**，对应 `RequestLog.cost = None`。写入时 `cost` 为 None 的行所有 `*_cost_usd` 字段是 `'0'` 字符串。`logger.rs:331-333` 会在这种情形下 warn（"模型定价未找到，成本将记录为 0"），但仍正常写入。

`backfill_missing_usage_costs` 后续会**定期扫描**这种 0 成本行（有 usage 的），按当前定价补算。

---

## 5. `UsageLogger` 写入路径

### 5.1 `log_with_calculation`（写入入口）

```rust
// logger.rs:307-362
pub fn log_with_calculation(
    request_id, provider_id, app_type, model, request_model, pricing_model,
    usage: TokenUsage, cost_multiplier: Decimal,
    latency_ms, first_token_ms, status_code,
    session_id, provider_type, is_streaming,
) -> Result<()> {
    let pricing = self.get_model_pricing(&pricing_model)?;
    
    let has_usage = usage.input_tokens > 0 || usage.output_tokens > 0
                 || usage.cache_read_tokens > 0 || usage.cache_creation_tokens > 0;
    if pricing.is_none() && has_usage && !is_placeholder_pricing_model(&pricing_model) {
        log::warn!("[USG-002] 模型定价未找到，成本将记录为 0: {pricing_model}");
    }
    
    let cost = CostCalculator::try_calculate_for_app(&app_type, &usage, pricing.as_ref(), cost_multiplier);
    
    let log = RequestLog { request_id, provider_id, app_type, model, request_model, pricing_model,
                          usage, cost, latency_ms, ... };
    self.log_request(&log)
}
```

### 5.2 `log_request`（写库）

```rust
// logger.rs:50-115
let (input_cost, output_cost, cache_read_cost, cache_creation_cost, total_cost) =
    if let Some(cost) = &log.cost { (cost.input_cost.to_string(), ...) }
    else { ("0".to_string(), "0".to_string(), "0".to_string(), "0".to_string(), "0".to_string()) };

let created_at = chrono::Utc::now().timestamp();

conn.execute(
    "INSERT OR REPLACE INTO proxy_request_logs (...) VALUES (?1, ?2, ...)",
    rusqlite::params![log.request_id, ..., created_at],
)?;

crate::usage_events::notify_log_recorded();  // 200ms 防抖
```

**写完即通知**——前端 Dashboard 通过 `useUsageEventBridge` 收到 `usage-log-recorded` 事件后 invalidate 所有 usage query，**无需等 30s 轮询**。

### 5.3 `log_error` / `log_error_with_context`

错误请求（status_code >= 400，无 usage）走这个分支。`pricing_model` 留空（`String::new()`），`usage` 是 `TokenUsage::default()`，`cost` 是 `None`。错误行 `total_cost_usd = '0'`，但保留 latency_ms / error_message / status_code 用于诊断。

---

## 6. `resolve_pricing_config`（定价配置解析）

`logger.rs:211-303` 决定 `cost_multiplier` 与 `pricing_model_source`：

```rust
// 1. 全局默认（按 app_type，proxy_config 表 3 行）
let default_app_type = if app_type == "claude-desktop" { "claude" } else { app_type };
let default_multiplier = self.db.get_default_cost_multiplier(default_app_type).await;
let default_pricing_source = self.db.get_pricing_model_source(default_app_type).await;

// 2. 供应商 meta 覆盖
let provider = self.db.get_provider_by_id(provider_id, app_type).ok().flatten();
let (provider_multiplier, provider_pricing_source) = provider.as_ref()
    .and_then(|p| p.meta.as_ref())
    .map(|meta| (meta.cost_multiplier.as_deref(), meta.pricing_model_source.as_deref()))
    .unwrap_or((None, None));

// 3. 合并
let cost_multiplier = provider_multiplier    // 优先供应商
    .and_then(|v| Decimal::from_str(v).ok())
    .unwrap_or(default_multiplier);
let pricing_model_source = match provider_pricing_source {
    Some("response" | "request") => provider_pricing_source,
    Some(other) => { warn; default_pricing_source.clone() }
    None => default_pricing_source.clone(),
};
```

**`pricing_model_source` 取值**（常量在 `database/mod.rs`）：
- `PRICING_SOURCE_RESPONSE` —— 按响应中真实模型计价（默认）
- `PRICING_SOURCE_REQUEST` —— 按客户端请求模型计价（用于路由接管场景）

`pricing_model` 字段在 handlers 层根据 `pricing_model_source` 解析后传入 `log_with_calculation`。

---

## 7. 测试覆盖

`parser.rs` 末尾的测试覆盖：
- `test_claude_response_parsing` / `test_claude_stream_parsing` —— 基本非流/流
- `test_claude_stream_prefers_smaller_delta_input_and_cache_pair` —— Qwen-max delta 修正
- `test_claude_stream_updates_cache_pair_from_later_delta_input` —— 多次 delta 同源
- `test_claude_stream_keeps_start_when_delta_input_is_larger` —— delta 变大不覆盖
- `test_codex_response_auto_openai_format` / `test_codex_response_auto_codex_format` —— 自动检测
- `test_codex_response_adjusted_saturating_sub` —— cache > input 不下溢
- `test_gemini_response_with_thoughts` —— 真实 thoughtsTokenCount 场景
- `test_has_billable_tokens_gates_empty_usage` —— 全 0 usage 闸门
- `test_claude_stream_cache_only_request_is_recorded` —— P2 回归（cache-only 请求必须保留）

`calculator.rs` 末尾：
- `test_cost_calculation` —— Claude 语义
- `test_cost_calculation_for_cache_inclusive_app` —— Codex 语义（`input - cache_read` 生效）
- `test_cost_multiplier` —— 倍率只乘总价
- `test_unknown_model_handling` —— pricing = None → None
- `test_decimal_precision` —— 高精度不退化

`logger.rs` 末尾：
- `test_log_request` —— 完整写入路径
- `test_log_error` —— 错误行写入
