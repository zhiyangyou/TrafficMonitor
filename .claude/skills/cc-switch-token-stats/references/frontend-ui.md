# 前端 UI 与数据流详解

> 本文深入 `src/components/usage/*` / `src/lib/api/usage.ts` / `src/lib/query/usage.ts` / `src/hooks/useUsageEventBridge.ts`，
> 重点说清 "UI 怎么展示" 与 "实时刷新怎么做"。

---

## 1. 组件树

```
UsageDashboard (总入口, 管理全局筛选状态)
├── 应用筛选按钮组 (all / claude / codex / gemini / opencode)
├── Provider Select (动态下拉)
├── Model Select (级联)
├── 刷新频率 Select (0 / 5s / 10s / 30s / 60s)
├── UsageDateRangePicker (5 预设 + 自定义)
├── UsageHero (顶部 4 个聚合卡片)
├── UsageTrendChart (recharts Area Chart, 双 Y 轴)
├── Tabs:
│   ├── RequestLogTable (分页 + 状态码筛选)
│   ├── ProviderStatsTable
│   └── ModelStatsTable
└── Accordion:
    └── PricingConfigPanel (定价编辑, 默认折叠)
```

主要文件：

| 文件 | 作用 |
|---|---|
| `UsageDashboard.tsx` | 总入口,管理全局状态,组装所有子组件 |
| `UsageHero.tsx` | 顶部 4 个聚合卡(总请求/总成本/输入/输出/缓存写入/缓存命中/命中率) |
| `UsageTrendChart.tsx` | recharts Area Chart,自动判定小时/日粒度,双 Y 轴 |
| `RequestLogTable.tsx` | 请求日志明细表,分页 + 状态码筛选 |
| `RequestDetailPanel.tsx` | 单条请求详情弹窗 |
| `ProviderStatsTable.tsx` | 按 provider 聚合 |
| `ModelStatsTable.tsx` | 按 model 聚合 |
| `UsageDateRangePicker.tsx` | 时间选择器(5 预设 + 日历) |
| `PricingConfigPanel.tsx` + `PricingEditModal.tsx` | 定价 CRUD |
| `DataSourceBar.tsx` | 数据来源标识(proxy / session / 各会话源) |
| `format.ts` | 数字/USD/token 格式化(`fmtInt` / `fmtUsd` / `formatTokensShort` 多语言) |

---

## 2. 三层抽象

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

### 2.1 API 层 (`src/lib/api/usage.ts`)

12+ 个方法对应 12+ 个 Tauri command：

| 方法 | 对应 command |
|---|---|
| `getUsageSummary(start, end, app, provider, model)` | `get_usage_summary` |
| `getUsageSummaryByApp(start, end, provider, model)` | `get_usage_summary_by_app` |
| `getUsageTrends(start, end, app, provider, model)` | `get_usage_trends` |
| `getProviderStats(...)` | `get_provider_stats` |
| `getModelStats(...)` | `get_model_stats` |
| `getRequestLogs(filters, page, pageSize)` | `get_request_logs` |
| `getRequestDetail(requestId)` | `get_request_detail` |
| `getModelPricing()` | `get_model_pricing` |
| `updateModelPricing(...)` | `update_model_pricing` |
| `deleteModelPricing(modelId)` | `delete_model_pricing` |
| `checkProviderLimits(providerId, appType)` | `check_provider_limits` |
| `syncSessionUsage()` | `sync_session_usage` |
| `getUsageDataSources(start, end)` | `get_usage_data_sources` |

### 2.2 React Query 层 (`src/lib/query/usage.ts`)

定义 `usageKeys` 缓存键：

```typescript
export const usageKeys = {
  all: ['usage'] as const,
  summary: (preset, customStart, customEnd, filters) =>
    ['usage', 'summary', preset, customStart, customEnd, filters] as const,
  summaryByApp: (preset, customStart, customEnd, filters) => ...,
  trends: (preset, customStart, customEnd, filters) => ...,
  providers: (preset, customStart, customEnd, filters) => ...,
  models: (preset, customStart, customEnd, filters) => ...,
  logs: (preset, customStart, customEnd, filters, page, pageSize) => ...,
  detail: (requestId) => ...,
  pricing: () => ['usage', 'pricing'] as const,
  dataSources: (preset, customStart, customEnd) => ...,
};
```

**缓存键包含所有筛选维度**——同一 hook 在不同筛选下走不同缓存。

每个 hook 都有 `staleTime: 30_000`（30 秒）+ `refetchOnWindowFocus`。

### 2.3 时间范围工具 (`src/lib/usageRange.ts`)

`resolveUsageRange(selection)` 把 `UsageRangeSelection` 翻译成 `startDate` / `endDate` Unix 时间戳：

```typescript
type UsageRangePreset = "today" | "1d" | "7d" | "14d" | "30d" | "custom";
type UsageRangeSelection = {
  preset: UsageRangePreset;
  customStartDate?: number;  // unix ms
  customEndDate?: number;
};

// "today" → [今天 00:00, now]
// "1d" / "7d" / "14d" / "30d" → [now - N*24h, now]
// "custom" → [customStartDate, customEndDate]
```

---

## 3. Dashboard 顶栏筛选

### 3.1 控件

| 维度 | 控件 | 选项来源 |
|---|---|---|
| App Type | 4 个带图标的 toggle 按钮 | 固定 `KNOWN_APP_TYPES = ['claude', 'codex', 'gemini', 'opencode']` |
| Provider | Select 动态下拉 | `useProviderStats` 实际有数据的 provider 列表 |
| Model | Select 动态下拉 | 随 Provider **级联**；选项来自 `useModelStats` |
| 时间范围 | `UsageDateRangePicker` | 5 个预设 + 自定义日历 + 时分精度 |
| 刷新频率 | Select | 0（关闭）/ 5s / 10s / 30s / 60s |
| 状态码（仅日志 Tab） | Select | All / 200 / 400 / 401 / 429 / 500 |

### 3.2 切换行为

- **切换 App Type**：清掉 Provider 和 Model（避免幽灵数据）
- **切换 Provider**：清掉 Model（级联）
- **切换时间范围**：所有 hook 立即 refetch
- **改刷新频率**：`invalidateQueries` 立即拉一次

### 3.3 筛选传到后端

筛选条件作为 Tauri invoke 参数直接传给后端，**无客户端二次过滤**。所有 hook 都把 `range` + `filters` 传进 `usageApi.getXxx(...)`，后端在 SQL WHERE 里直接用。

---

## 4. `UsageHero` 详解

### 4.1 顶部 4 个聚合卡片

```
┌──────────────┬──────────────┬──────────────┬──────────────┐
│ 总请求数     │ 总成本 (USD) │ 输入 tokens  │ 输出 tokens  │
│   1,234      │   $12.45     │   1.2M       │   456K       │
└──────────────┴──────────────┴──────────────┴──────────────┘
┌──────────────┬──────────────┐
│ 缓存写入     │ 缓存命中     │
│  234K (N/A)  │  678K / 75%  │  ← cacheHitRate
└──────────────┴──────────────┘
```

### 4.2 `pickSummary(allApps, appType)`

```typescript
function pickSummary(summaries: UsageSummaryByApp[], appType: AppTypeFilter): UsageSummary {
  if (appType === "all") {
    return aggregateSummaries(summaries);  // 客户端累加
  }
  return summaries.find(s => s.appType === appType)?.summary ?? emptySummary();
}
```

`aggregateSummaries` 把多行 `UsageSummary` 累加：
- `totalRequests / totalCost / totalInputTokens / totalOutputTokens / ...` 直接累加
- `successRate` = `sum(2xx count) / sum(all count)`（重新从合计计数派生，**不是平均**）
- `realTotalTokens` = 累加
- `cacheHitRate` = `sum(cacheRead) / sum(freshInput + cacheCreation + cacheRead)`（**不是平均**）

### 4.3 cache-inclusive 协议的特殊展示

`UsageHero` 在 `cacheCreationTokens` 列对 OpenAI 协议（codex / gemini）显示 **"N/A — 协议不上报"** 提示，而不是 `0`——避免误导用户以为没产生缓存写。

### 4.4 关键代码片段

```typescript
// UsageHero.tsx 简化版
const { data: byApp } = useUsageSummaryByApp(range, filters);
const summary = useMemo(() => pickSummary(byApp ?? [], appType), [byApp, appType]);

// cache-inclusive 协议: cache 写显示 N/A
const showCacheWriteNA = CACHE_INCLUSIVE_APP_TYPES.has(appType);
```

---

## 5. `UsageTrendChart` 详解

### 5.1 图表形式

recharts `AreaChart`：
- X 轴：日期（`>= 24h`）或小时（`< 24h`）
- 左 Y 轴：tokens（堆叠 Area）
- 右 Y 轴：成本 USD
- 4 个 Area：input / output / cacheCreation / cacheRead
- 自定义 Tooltip，hover 显示具体数字
- linearGradient 渐变填充

### 5.2 小时/日粒度自动切换

```typescript
const isHourly = durationMs <= 24 * 60 * 60 * 1000;
// < 24h → 按小时显示 24 个点
// >= 24h → 按天显示
```

### 5.3 数据流

`useUsageTrends(range, filters)` → 后端 `get_usage_trends` → `Vec<DailyStats>`（每日 1 行）。

```typescript
// DailyStats 类型
interface DailyStats {
  date: string;              // 'YYYY-MM-DD' 或 'YYYY-MM-DD HH'
  requestCount: number;
  totalCost: string;
  totalTokens: number;       // 累加
  totalInputTokens: number;
  totalOutputTokens: number;
  totalCacheCreationTokens: number;
  totalCacheReadTokens: number;
}
```

### 5.4 cache 字段归一

`UsageTrendChart` 内部对每个 `DailyStats` 调用 `getFreshInputTokens` 做 cache 归一（仅展示层，不改数据）。

---

## 6. `RequestLogTable` 详解

### 6.1 列

| 列 | 来源 | 备注 |
|---|---|---|
| 时间 | `createdAt` | 本地时区格式化 |
| Provider | `providerName` | 来自 `provider_id` 关联 |
| App | `appType` | 图标 + 文本 |
| Model | `model` | hover 显示 `requestModel` / `pricingModel` |
| 输入/输出/缓存 | tokens 字段 | cache 写对 OpenAI 协议显示 N/A |
| 成本 | `totalCostUsd` | 美元格式 |
| 延迟 | `latencyMs` | 流式显示 `firstTokenMs` |
| 状态 | `statusCode` | 颜色编码 |
| 操作 | — | 点击行 → `RequestDetailPanel` |

### 6.2 分页

```typescript
const { data } = useRequestLogs(range, filters, page, pageSize);
// data: { data: RequestLog[], total, page, pageSize }
```

`page` 从 1 开始，`pageSize` 默认 50。底部有分页器。

### 6.3 状态码筛选

`RequestLogTable` 内部维护 `statusCode` 状态，加入 hook 的 queryKey。

### 6.4 跨源去重的展示

`RequestLogTable` 看到的行有 `dataSource` 字段（可选），UI 上不一定显示，但 `RequestDetailPanel` 详情里会显示 `data_source` 区分 proxy / session_log / codex_session 等。

---

## 7. `useUsageEventBridge` 实时刷新

### 7.1 后端事件

```rust
// usage_events.rs
pub fn notify_log_recorded() {
    // 200ms 防抖合并, 防止代理高频请求时事件风暴
    debouncer.debounce(200ms, || {
        app_handle.emit_all("usage-log-recorded", ()).ok();
    });
}
```

每次 `INSERT proxy_request_logs` 成功（`logger.rs:112`）都调用。**所有**写入都会触发——proxy 拦截、session 同步、rollup 剪枝。

### 7.2 前端桥

```typescript
// useUsageEventBridge.ts 简化
useEffect(() => {
  const unlisten = listen("usage-log-recorded", () => {
    queryClient.invalidateQueries({ queryKey: usageKeys.all });
  });
  return unlisten;
}, [queryClient]);
```

`UsageDashboard` 挂载时绑定事件，卸载时解绑。**收到事件后 `invalidateQueries` 让所有 usage 缓存失效，下一次访问 hook 立即 refetch**。

### 7.3 与轮询的关系

Dashboard 顶栏的"刷新频率"是**轮询**（5/10/30/60s）。但实际使用中事件桥已经把延迟压到亚秒级——轮询是兜底。改"刷新频率"为 0（关闭）不影响事件桥。

---

## 8. 关键类型与派生函数

### 8.1 `getFreshInputTokens` (`types/usage.ts:212-220`)

```typescript
export function getFreshInputTokens(log: CacheNormalizableLog): number {
  if (
    CACHE_INCLUSIVE_APP_TYPES.has(log.appType) &&
    log.inputTokens >= log.cacheReadTokens
  ) {
    return log.inputTokens - log.cacheReadTokens;
  }
  return log.inputTokens;
}
```

前端 cache 归一的唯一入口。与后端 `fresh_input_sql` 语义完全一致。

### 8.2 `hasUsageTokens` (`types/usage.ts:241-248`)

```typescript
export function hasUsageTokens(log: UsageCostLog): boolean {
  return (
    log.inputTokens > 0 || log.outputTokens > 0
    || log.cacheReadTokens > 0 || log.cacheCreationTokens > 0
  );
}
```

前端判断"是否有计费 token"。后端对应 `TokenUsage::has_billable_tokens()`。

### 8.3 `isUnpricedUsage` (`types/usage.ts:250-264`)

```typescript
export function isUnpricedUsage(log: UsageCostLog): boolean {
  const totalCost = Number.parseFloat(log.totalCostUsd);
  const multiplier = log.costMultiplier == null
    ? undefined : Number.parseFloat(log.costMultiplier);
  return (
    log.statusCode >= 200 && log.statusCode < 300
    && hasUsageTokens(log)
    && Number.isFinite(totalCost)
    && (!Number.isFinite(multiplier) || multiplier !== 0)
    && totalCost === 0
  );
}
```

**前端识别"未计价行"**——用于显示警告徽章、等待 backfill 后消失。

判断条件：
- 状态码 2xx
- 有 usage token
- `totalCostUsd` 是有效数字且 == 0
- `costMultiplier` 不为 0

注意：如果 `costMultiplier === 0`（供应商设置为 0 倍率），不算未计价——是用户主动设置的"不计费"。

### 8.4 `realTotalTokens` 与 `cacheHitRate`

后端在 `usage_stats.rs` 派生后放入 `UsageSummary`，前端不再二次计算（除 `UsageHero` 的 "all" 聚合）。

```typescript
interface UsageSummary {
  totalRequests: number;
  totalCost: string;
  totalInputTokens: number;
  totalOutputTokens: number;
  totalCacheCreationTokens: number;
  totalCacheReadTokens: number;
  successRate: number;
  /** input + output + cache_creation + cache_read, all cache-normalized */
  realTotalTokens: number;
  /** cache_read / (input + cache_creation + cache_read), range 0–1 */
  cacheHitRate: number;
}
```

---

## 9. 格式化 (`format.ts`)

### 9.1 `formatTokensShort` 多语言

```typescript
formatTokensShort(1_500_000, 'zh')  // "150万"
formatTokensShort(1_500_000, 'en')  // "1.5M"
formatTokensShort(1_500_000_000, 'zh')  // "15亿"
formatTokensShort(1_500_000_000, 'en')  // "1.5B"
```

支持 zh / zh-TW（万/亿/萬/億） / en / ja 等。

### 9.2 `fmtUsd`

```typescript
fmtUsd('0.012345')  // "$0.0123"
fmtUsd('12.5')      // "$12.50"
```

保留 2-4 位小数（按数量级自动）。

### 9.3 `fmtInt`

```typescript
fmtInt(1234567)  // "1,234,567" (en) / "1,234,567" (zh) — 千分位
```

---

## 10. `UsageDateRangePicker` 详解

### 10.1 5 个预设

| 预设 | 范围 |
|---|---|
| Today | `[今天 00:00, now]` |
| 1d | `[now - 24h, now]` |
| 7d | `[now - 7*24h, now]` |
| 14d | `[now - 14*24h, now]` |
| 30d | `[now - 30*24h, now]` |

### 10.2 Custom

Popover + 日历 + 时间 input（精度 1 分钟）。起止自动 swap。

### 10.3 边界对齐

- "Today" 自动对齐到本地 00:00
- 1d/7d/14d/30d 用 `Date.now() - N*86400000`，不强制对齐
- Custom 完全用户控制

---

## 11. `PricingConfigPanel` 详解

### 11.1 列表

通过 `useQuery` 加载 `getModelPricing()`，显示所有已 seed 的模型：
- `modelId` / `displayName`
- 4 个价格字段（input / output / cacheRead / cacheCreation per 1M）
- 操作：编辑 / 删除

### 11.2 编辑

点击"编辑"打开 `PricingEditModal`，4 个 input（数字），保存后调 `updateModelPricing`：
- 后端会**触发 backfill**——所有 `total_cost_usd = 0` 且有 usage 的历史行按新价重算
- 完成后 `usageEvents::notify_log_recorded` 让前端重拉

### 11.3 删除

`deleteModelPricing(modelId)`。删除后该模型的新请求 cost = 0（warning），等待下次 seed 或重新添加。

---

## 12. 截图位置

官方文档中的 token 统计截图（中文/英文手册同位置）：

- `docs/user-manual/zh/4-proxy/4.4-usage.md`
- `docs/user-manual/en/4-proxy/4.4-usage.md`

实际图片位于 `docs/user-manual/assets/`：
- `image-20260108011730105.png` — 时间范围选择器
- `image-20260108011742847.png` — Token 趋势图
- `image-20260108011859974.png` — 请求日志表
- `image-20260108011907928.png` — Provider 统计
- `image-20260108011915381.png` — Model 统计
- `image-20260108011933565.png` — 定价配置

Release Notes 中也有提及：
- `docs/release-notes/v3.15.0-en.md` / `v3.14.0-*.md` / `v3.13.0-*.md`
- v3.15.0 引入 filter-driven Hero

---

## 13. 关键易错点

1. **AppType `claude-desktop` 不在 `KNOWN_APP_TYPES` 中**：但后端会把它折叠进 `claude`，所以前端不显示 "Claude Desktop" 筛选按钮。后端会查 `claude-desktop` 的真实记录。

2. **`model` vs `requestModel` vs `pricingModel`**：UI 默认显示 `model`，详情面板 hover 显示另外两个。`ModelStats` 表按 `pricing_model` 优先分组（`effective_model_sql`）。

3. **`cache_creation_tokens === 0` 对 OpenAI 协议是协议不上报**：不要当成"未使用缓存"。

4. **Dashboard 的 "all" 聚合不等于数据库 SUM**：前端 `aggregateSummaries` 客户端累加，**与后端"不指定 app_type 的全量查询"结果一致**（不包含跨源去重掉的行）。

5. **事件桥 200ms 防抖**：高频请求下 1 秒内可能收到 10+ 次 `usage-log-recorded`，但前端只会 invalidate 一次（合并）。
