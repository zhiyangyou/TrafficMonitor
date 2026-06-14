# 持久化与 Rollup 详解

> 本文深入 `src-tauri/src/database/schema.rs` / `database/dao/usage_rollup.rs` / `services/usage_stats.rs` / `services/sql_helpers.rs`，
> 重点说清 "明细表 + 聚合表双层结构"、"rollup 不可逆原则" 和 "跨源去重"。

---

## 1. 数据库总览

| 项 | 值 |
|---|---|
| 驱动 | `rusqlite 0.31` (bundled) |
| 文件 | `~/.cc-switch/cc-switch.db` |
| 当前版本 | `SCHEMA_VERSION = 11`（`database/mod.rs:52`） |
| 启动顺序 | `create_tables()` → `apply_schema_migrations()` → `rollup_and_prune(30)` → 周期任务 |
| 周期任务 | session 同步每 60s（`lib.rs:1071`）；rollup 由 `notify` 事件链触发 |

---

## 2. 关键表结构

### 2.1 `proxy_request_logs`（明细表）

```sql
-- schema.rs:186-199
CREATE TABLE IF NOT EXISTS proxy_request_logs (
    request_id TEXT PRIMARY KEY,
    provider_id TEXT NOT NULL,
    app_type TEXT NOT NULL,         -- claude / codex / gemini / claude-desktop / opencode / openclaw / hermes
    model TEXT NOT NULL,
    request_model TEXT,             -- v11+: 客户端请求的原始模型名（路由接管下与 model 不同）
    pricing_model TEXT,             -- v11+: 写入时实际用于计价的模型名
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
    created_at INTEGER NOT NULL,    -- unix timestamp, UTC 秒
    data_source TEXT NOT NULL DEFAULT 'proxy'  -- proxy / session_log / codex_session / gemini_session / opencode_session
);
```

**索引**：
- `idx_request_logs_provider` (`provider_id`, `app_type`)
- `idx_request_logs_created_at` (`created_at`) —— 时间范围查询主力
- `idx_request_logs_model` (`model`)
- `idx_request_logs_session` (`session_id`)
- `idx_request_logs_status` (`status_code`)
- `idx_request_logs_app_created` (`app_type`, `created_at DESC`)
- `idx_request_logs_dedup_lookup_expr`（表达式索引，跨源去重性能优化）

### 2.2 `usage_daily_rollups`（聚合表）

```sql
-- schema.rs:264-283
CREATE TABLE IF NOT EXISTS usage_daily_rollups (
    date TEXT NOT NULL,                              -- 'YYYY-MM-DD' 本地
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

**主键包含 6 列**——这意味着同一 `(date, app_type, provider_id, model)` 下，路由接管产生的不同 `request_model` 或 request 计价模式下的不同 `pricing_model` 都各自成行（`usage_rollup.rs` 的测试 `test_rollup_preserves_*_dimension` 覆盖）。

**注意**：聚合表**只保留 `total_cost_usd`**（不分项），也不保留 `latency_ms`（只保留 `avg_latency_ms`）、`first_token_ms`、`duration_ms`、`error_message`——这些都是明细级字段。

### 2.3 `model_pricing`（定价表）

```sql
-- schema.rs:222-232
CREATE TABLE IF NOT EXISTS model_pricing (
    model_id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    input_cost_per_million TEXT NOT NULL,        -- USD/1M tokens
    output_cost_per_million TEXT NOT NULL,
    cache_read_cost_per_million TEXT NOT NULL DEFAULT '0',
    cache_creation_cost_per_million TEXT NOT NULL DEFAULT '0'
);
```

**seed**：`seed_model_pricing()`（`schema.rs:1276-2104`）启动时执行，覆盖 100+ 主流模型（Claude 全系 / GPT 全系 / Gemini / DeepSeek / Kimi / GLM / Qwen 等）。

**运行时编辑**：`update_model_pricing` 命令（`commands/usage.rs:174-248`）通过 UI 的 `PricingConfigPanel` 编辑后回写。

### 2.4 `session_log_sync`（增量同步状态）

```sql
-- schema.rs:287-296
CREATE TABLE IF NOT EXISTS session_log_sync (
    file_path TEXT PRIMARY KEY,
    last_modified INTEGER NOT NULL,
    last_line_offset INTEGER NOT NULL DEFAULT 0,
    last_synced_at INTEGER NOT NULL
);
```

记录每个 JSONL 文件的 `mtime`（nanos 精度）和上次读到的行偏移，下次同步跳过未变部分。

---

## 3. Rollup 算法（核心）

### 3.1 Cutoff 对齐到本地午夜

```rust
// usage_rollup.rs:19-55
fn compute_local_midnight_cutoff(now, retain_days) -> i64 {
    let target_day = (now - retain_days.days()).date_naive();
    let next_day = target_day.succ_opt()?;  // target_day + 1
    let naive_midnight = next_day.and_hms_opt(0, 0, 0)?;
    let local_dt = Local.from_local_datetime(&naive_midnight);
    // DST 处理: Ambiguous 取 earliest, None 退到 +1h
    Ok(local_dt.timestamp())
}
```

**为什么对齐到下一天午夜**：
- 保留 `retain_days` 天的明细 = 保留到 `now - retain_days` 之**前**的完整日
- 如果 `now = 2026-04-16 14:32`、`retain_days = 30`：target = `2026-03-17`，cutoff = `2026-03-18 00:00 local`
- 保留范围 `[2026-03-18 00:00, 2026-04-16 14:32]` = 29 天零 14 小时
- 被剪的是 `2026-03-18 00:00` 之前的所有数据——**全是完整本地日**

如果 cutoff 落在一天中段，那天会被半 rollup / 半 prune，下次查询涉及该日时会漏算。

### 3.2 剪枝前先 backfill

```rust
// usage_rollup.rs:78-85
// 剪枝是不可逆的：明细一旦汇总删除，0 成本行就永远失去按 pricing_model
// 补价重算的机会（启动序列里 seed 定价先于 rollup、但启动回填在 rollup
// 之后；周期任务同理）。所以剪枝前先尽力回填一次。
if let Err(e) = Self::backfill_missing_usage_costs_on_conn(&conn, None) {
    log::warn!("Pre-prune cost backfill failed, pruning anyway: {e}");
}
```

`backfill_missing_usage_costs` 扫描 `total_cost_usd = '0'` 且有 usage 的行，按 `pricing_model` 的当前定价重算。**失败仅告警不阻断**——一行损坏的定价数据不应永久卡死日志清理。

### 3.3 Rollup 聚合 SQL

```sql
-- usage_rollup.rs:121-167
INSERT OR REPLACE INTO usage_daily_rollups
    (date, app_type, provider_id, model, request_model, pricing_model,
     request_count, success_count,
     input_tokens, output_tokens,
     cache_read_tokens, cache_creation_tokens,
     total_cost_usd, avg_latency_ms)
SELECT
    d, a, p, m, rm, pm,
    COALESCE(old.request_count, 0) + new_req,
    COALESCE(old.success_count, 0) + new_succ,
    COALESCE(old.input_tokens, 0) + new_in,
    COALESCE(old.output_tokens, 0) + new_out,
    COALESCE(old.cache_read_tokens, 0) + new_cr,
    COALESCE(old.cache_creation_tokens, 0) + new_cc,
    CAST(COALESCE(CAST(old.total_cost_usd AS REAL), 0) + new_cost AS TEXT),
    CASE WHEN COALESCE(old.request_count, 0) + new_req > 0
        THEN (COALESCE(old.avg_latency_ms, 0) * COALESCE(old.request_count, 0)
              + new_lat * new_req)
             / (COALESCE(old.request_count, 0) + new_req)
        ELSE 0 END
FROM (
    SELECT
        date(l.created_at, 'unixepoch', 'localtime') as d,
        l.app_type as a, l.provider_id as p, l.model as m,
        COALESCE(l.request_model, '') as rm,
        COALESCE(l.pricing_model, '') as pm,
        COUNT(*) as new_req,
        SUM(CASE WHEN l.status_code >= 200 AND l.status_code < 300 THEN 1 ELSE 0 END) as new_succ,
        COALESCE(SUM(l.input_tokens), 0) as new_in,
        COALESCE(SUM(l.output_tokens), 0) as new_out,
        COALESCE(SUM(l.cache_read_tokens), 0) as new_cr,
        COALESCE(SUM(l.cache_creation_tokens), 0) as new_cc,
        COALESCE(SUM(CAST(l.total_cost_usd AS REAL)), 0) as new_cost,
        COALESCE(AVG(l.latency_ms), 0) as new_lat
    FROM proxy_request_logs l
    WHERE l.created_at < ?1 AND {effective_usage_log_filter("l")}
    GROUP BY d, a, p, m, rm, pm
) agg
LEFT JOIN usage_daily_rollups old
    ON old.date = agg.d AND old.app_type = agg.a
   AND old.provider_id = agg.p AND old.model = agg.m
   AND old.request_model = agg.rm AND old.pricing_model = agg.pm
```

**关键点**：
- 用 `date(created_at, 'unixepoch', 'localtime')` 转为**本地日**（不是 UTC 日）。
- 用 `effective_usage_log_filter("l")` 排除跨源去重中被跳过的行。
- 用 `LEFT JOIN ... old` 合并已有 rollup 行（实现"幂等增量"）——同 (date, app, provider, model, request_model, pricing_model) 重复执行不会重复加。
- `avg_latency_ms` 加权平均：`(old_avg × old_count + new_avg × new_count) / (old_count + new_count)`。

### 3.4 DELETE 旧明细

```sql
DELETE FROM proxy_request_logs WHERE created_at < ?1
```

**注意**：DELETE 用**全表**的 `created_at < cutoff`，不重复 `effective_usage_log_filter`——跨源去重中被跳过的 session 行也一起删（它们已经在 rollup 阶段被排除了，再留着是浪费空间）。

### 3.5 完整流程

```
1. compute_local_midnight_cutoff(now, 30) → cutoff timestamp
2. 检查 proxy_request_logs 是否有 created_at < cutoff 的行，没有则 return 0
3. backfill_missing_usage_costs() → 给 0 成本行补价（容错，失败告警）
4. SAVEPOINT rollup_prune
5. INSERT OR REPLACE INTO usage_daily_rollups SELECT ... FROM proxy_request_logs WHERE ...
6. DELETE FROM proxy_request_logs WHERE created_at < cutoff
7. RELEASE rollup_prune
8. notify_log_recorded()  // 通知前端数据已聚合
```

---

## 4. 跨源去重

### 4.1 问题

proxy 拦截的请求可能在 session 日志里也有记录（同一请求被两个通道都抓到）。如果不去重，会双算。

### 4.2 方案

`effective_usage_log_filter`（`usage_stats.rs`）+ `should_skip_session_insert`（在 `session_usage*.rs` 中）。

**核心思路**：
- session 同步时（`should_skip_session_insert`）：检查 `±10min` 窗口内是否已有 `data_source = 'proxy'` 行。如果有，跳过这条 session 行。
- 聚合查询时（`effective_usage_log_filter`）：排除"同 (provider_id, app_type) + 10min 窗口内有 proxy 行的 session 行"。

### 4.3 `effective_usage_log_filter` 大致形式

```sql
WHERE NOT EXISTS (
    SELECT 1 FROM proxy_request_logs p2
    WHERE p2.data_source = 'proxy'
      AND p2.provider_id = l.provider_id
      AND p2.app_type = l.app_type
      AND ABS(p2.created_at - l.created_at) < 600   -- 10 分钟
)
```

具体实现可能用表达式索引优化（`idx_request_logs_dedup_lookup_expr`），但语义就是这个。

### 4.4 provider_id 命名

session 行的 `provider_id` 形如 `_session` / `_codex_session` / `_gemini_session` / `_opencode_session`，与真实 provider id 不冲突。跨源去重时这些 id 永远不会匹配 proxy 行（proxy 行的 `provider_id` 是真实 provider id），所以去重只在"同 app_type 跨源"层面进行。

---

## 5. `fresh_input_sql` 关键助手

```rust
// sql_helpers.rs:31-47
const CACHE_INCLUSIVE_APP_TYPES: &[&str] = &["codex", "gemini"];

pub fn fresh_input_sql(alias: &str) -> String {
    let prefix = if alias.is_empty() { "" } else { "{alias}." };
    let app_type_list = CACHE_INCLUSIVE_APP_TYPES.iter()
        .map(|t| format!("'{t}'")).collect::<Vec<_>>().join(", ");
    format!(
        "CASE WHEN {prefix}app_type IN ({app_type_list}) AND {prefix}input_tokens >= {prefix}cache_read_tokens \
              THEN ({prefix}input_tokens - {prefix}cache_read_tokens) \
              ELSE {prefix}input_tokens END"
    )
}
```

**作用**：把 `input_tokens` 统一为"fresh input"口径，对 cache-inclusive 协议（codex / gemini）做减法。

**为什么 `input_tokens >= cache_read_tokens` 防御**：异常行 cache > input 时不减（避免负数）。

**调用方**：
- `usage_stats.rs` 的所有聚合查询（`get_usage_summary` / `get_usage_trends` / `get_provider_stats` / `get_model_stats` 等）
- rollup 不直接用（rollup 是简单 SUM，不做 cache 归一化）——rollup 表里存的还是原始 `input_tokens`

**白名单的设计哲学**（`sql_helpers.rs:14-19` 注释）：
> 新 provider 默认按 Claude 语义（"input 已排除 cache"）。反向错误（OpenAI 风格却没加进白名单）会暴露为"缓存命中率过低"——比"silent over-deduction"（白名单多加了）更容易被发现。

---

## 6. 聚合查询模式

### 6.1 `get_usage_summary` 模式

```sql
-- usage_stats.rs:505-659（核心查询）
SELECT
    COUNT(*) AS total_requests,
    CAST(SUM(CAST(total_cost_usd AS REAL)) AS TEXT) AS total_cost,
    SUM(input_tokens) AS total_input_tokens,
    SUM(output_tokens) AS total_output_tokens,
    SUM(cache_creation_tokens) AS total_cache_creation_tokens,
    SUM(cache_read_tokens) AS total_cache_read_tokens,
    AVG(CASE WHEN status_code BETWEEN 200 AND 299 THEN 1.0 ELSE 0.0 END) AS success_rate,
    -- realTotalTokens 和 cacheHitRate 后续在 Rust 派生
FROM proxy_request_logs
WHERE created_at BETWEEN ?start AND ?end
  AND {effective_usage_log_filter}
  AND (app_type = ?app_type OR ?app_type IS NULL)  -- 折叠 claude-desktop
  AND ...
```

**UNION 双源**：30 天前的范围查询需要 `UNION ALL` `usage_daily_rollups`，但只把**完全被查询范围覆盖**的 rollup 日期纳入（`compute_rollup_date_bounds`）——避免部分覆盖漏算。

### 6.2 `get_request_logs`（分页）

```sql
-- 标准分页: WHERE + ORDER BY created_at DESC LIMIT ?page_size OFFSET (?page-1)*?page_size
```

`page` 从 1 开始。`total` 用 `SELECT COUNT(*)` 同条件查。

### 6.3 `get_request_detail`

```sql
SELECT ... FROM proxy_request_logs WHERE request_id = ?
```

只查 `proxy_request_logs`——**没有对应的 rollup_detail 表**。所以超过 30 天的日志无法查详情（这是有意为之的）。

### 6.4 `get_usage_data_sources`

```sql
SELECT data_source, COUNT(*) AS request_count, SUM(CAST(total_cost_usd AS REAL)) AS total_cost
FROM proxy_request_logs
WHERE created_at BETWEEN ?start AND ?end
GROUP BY data_source
```

返回 `Vec<DataSourceSummary>`，给 `DataSourceBar` 组件显示。

---

## 7. Backfill 机制

### 7.1 何时触发

1. **启动时**：`rollup_and_prune` 之前
2. **剪枝前**：`rollup_and_prune` 内部
3. **定价变更时**：`update_model_pricing` 命令后（`commands/usage.rs:174-248`）
4. **失败时**：下次启动 + 下次剪枝

### 7.2 扫描条件

```sql
SELECT * FROM proxy_request_logs
WHERE total_cost_usd = '0'
  AND (input_tokens > 0 OR output_tokens > 0 OR cache_read_tokens > 0 OR cache_creation_tokens > 0)
  AND status_code BETWEEN 200 AND 299
  AND pricing_model != ''  -- v11+: 错误行 (pricing_model='') 跳过
```

### 7.3 重算逻辑

按 `pricing_model` 字段查 `model_pricing` 表，得到新单价，重新调用 `CostCalculator::calculate_for_app` 计算 `total_cost_usd` 与各分项。

**为什么按 `pricing_model` 而不是 `model`**：路由接管下 `model`（真实模型）可能与当时计价的 `pricing_model` 不同。历史行必须按**写入时的计价基准**重算。

---

## 8. Session 同步通道

### 8.1 四个源

| App | 文件 | 路径 | 关键解析 |
|---|---|---|---|
| **Claude Code** | `services/session_usage.rs` | `~/.claude/projects/<project>/*.jsonl`<br>+ subagents + `workflows/wf_*/` | 仅 `type=="assistant"`，按 `message.id` 去重（保留有 `stop_reason` 的），跳过全 0 token |
| **Codex** | `services/session_usage_codex.rs` | `~/.codex/sessions/YYYY/MM/DD/*.jsonl`<br>+ `~/.codex/archived_sessions/*.jsonl` | 累计值 `total_token_usage` → **delta 计算**（`compute_delta`） |
| **Gemini** | `services/session_usage_gemini.rs` | `~/.gemini/tmp/*/chats/session-*.json` | JSON（非 JSONL），每条 message 独立 tokens；**output = output + thoughts** |
| **OpenCode** | `services/session_usage_opencode.rs` | `~/.local/share/opencode/opencode.db` | 直接读 SQLite `session` + `message` 表 |

### 8.2 触发时机

- **启动时一次**（`lib.rs:1035-1093`）
- **每 60s 一次**（`lib.rs:1071` 附近）

### 8.3 增量同步

`session_log_sync.last_modified` 用 nanos 精度（`metadata_modified_nanos`）。文件未变（mtime 不变）就跳过整文件。

Codex 还需要维护 `last_line_offset` 防止 JSONL 截断写入。实际看代码用 mtime 跳过为主，offset 主要是占位字段。

### 8.4 写入规则

- `provider_id` 设为 `_session` / `_codex_session` / `_gemini_session` / `_opencode_session`
- `data_source` 设对应值（`session_log` / `codex_session` / `gemini_session` / `opencode_session`）
- `request_id` 形如 `session:{message_id}`（与 proxy 行的 `request_id` 共享同一 message_id 命名空间，便于跨源去重）
- `cost = None`（session 同步不走 calculate，但后续 backfill 可能补价）

---

## 9. Migrations 演进（v0→v11）

| 版本 | 关键变更 |
|---|---|
| v1→v2 | 创建 `proxy_request_logs` + `model_pricing` |
| v2→v3 | Skills 统一管理架构 |
| v3→v4 | OpenCode 支持 |
| v4→v5 | 计费模式支持（`pricing_model_source`） |
| v5→v6 | 创建 `usage_daily_rollups` + Copilot 模板统一 |
| v6→v7 | Skills 更新检测 |
| v7→v8 | 加 `data_source` + `session_log_sync` + 修正 13 个模型定价 |
| v8→v9 | 清空重 seed 全量模型定价 |
| v9→v10 | Hermes Agent 支持 |
| v10→v11 | `usage_daily_rollups` 主键加 `request_model` / `pricing_model`（SQLite 改主键需重建表） |

迁移用 SAVEPOINT 包裹（`schema.rs:367-470`），失败回滚到 savepoint。

---

## 10. 测试覆盖

`usage_rollup.rs` 末尾（`#[cfg(test)]`）：

- `cutoff_is_aligned_to_local_midnight_after_target_day` —— cutoff = next-day midnight
- `cutoff_at_local_midnight_now_still_lands_on_midnight` —— 边界：now 自身在 midnight
- `test_rollup_and_prune` —— 完整流程：40 天前的 5 条 rollup + 5 天前的 3 条保留
- `test_rollup_uses_effective_usage_logs` —— 跨源去重（codex 旧 proxy 行优先，session 行被排除）
- `test_rollup_preserves_request_model_dimension` —— 路由接管的 3 个 request_model 各自成行
- `test_rollup_preserves_pricing_model_dimension` —— request 计价模式 2 个 pricing_model 各自成行
- `test_rollup_backfills_costs_before_pruning` —— 0 成本行被 backfill 再 rollup
- `test_rollup_noop_when_no_old_data` —— 无旧数据直接 return 0
- `test_rollup_merges_with_existing` —— 旧 rollup 行 + 新明细正确合并（10+3=13, 1000+300=1300）

`sql_helpers.rs` 末尾：

- `fresh_input_with_alias_emits_prefixed_columns` / `fresh_input_without_alias_uses_bare_columns`
- `fresh_input_subtracts_cache_for_cache_inclusive_providers` —— Codex/Gemini 减，Claude 不减
- `fresh_input_handles_codex_with_cache_exceeding_input` —— 防御：cache > input 不下溢
