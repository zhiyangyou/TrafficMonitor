# Delta Algorithm

`CPerSessionAccumulator::OnStatuslineUpdate()` 是把 statusline 推送的 `current_usage` 4 个字段转换成"本回合增量"的地方。`DataManager::Tick()` 每秒 1 次拉取 sidecar，把每条 entry 喂给 `OnStatuslineUpdate`，汇总到 1Hz 滑动窗口，再归一化入环形缓冲。

## 输入

每次 statusline 推送一条 entry：

```cpp
struct StatuslineEntry {
    std::wstring session_id;
    std::wstring cwd;
    std::wstring model;
    unsigned long long input{};
    unsigned long long output{};
    unsigned long long cache_creation{};
    unsigned long long cache_read{};
    ULONGLONG received_ms{};   // 本进程读到的 wall clock
    bool parsed_ok{ false };
};
```

4 个 token 字段来自 `context_window.current_usage.{input_tokens, output_tokens, cache_creation_input_tokens, cache_read_input_tokens}`。

## 首次见到某 session

`SessionState::first_seen = true` 时，`OnStatuslineUpdate()` 的逻辑：

```cpp
Delta OnStatuslineUpdate(SessionKey key, unsigned long long input,
                         unsigned long long output, unsigned long long cc,
                         unsigned long long cr, ULONGLONG now_ms) {
    auto& s = m_states[key];
    if (s.first_seen) {
        s.last_input = input;
        s.last_cache_creation = cc;
        s.last_cache_read = cr;
        s.last_output = output;
        s.first_seen = false;
        s.session_start_ms = now_ms;
        s.last_seen_ms = now_ms;
        return Delta{0, 0, 0, 0, 0};   // dt_ms=0 表示"无增量"
    }
    // ...
}
```

首帧返回 `dt_ms=0`，`Tick()` 看到 `dt_ms==0` 就跳过、不入滑动窗口。baseline 被建立，后续帧开始算 delta。

## 后续帧的 delta 计算

```cpp
long long d_input   = (long long)input   - (long long)s.last_input;
long long d_cc      = (long long)cc      - (long long)s.last_cache_creation;
long long d_cr      = (long long)cr      - (long long)s.last_cache_read;
long long d_output  = (long long)output  - (long long)s.last_output;
s.last_input   = input;
s.last_cache_creation = cc;
s.last_cache_read     = cr;
s.last_output         = output;
s.last_seen_ms = now_ms;
```

返回值：
```cpp
Delta{d_input, d_cc, d_cr, d_output, dt_ms};
```

`dt_ms` 是 `now_ms - s.last_seen_ms`（两次 statusline 推送的间隔，由本进程 `GetTickCount64` 测得，跟 `wrapper_ms` 无关）。

## baseline 重置（倒退检测）

`current_usage` 在以下场景会出现"倒退"：
- `/compact` 后上下文被压缩，`cache_creation_input_tokens` 跳回接近 0
- `/clear` 后 `input_tokens` 从几万回到几千
- 主进程 bug 导致字段被重置

直接 `current - baseline` 会算出负数，破坏 1Hz 窗口求和。

检测方式：4 个字段任一出现 `current < baseline`（用 `long long` 算术自然产生负值）→ 视为新 baseline：

```cpp
bool any_negative = (d_input < 0) || (d_cc < 0) || (d_cr < 0) || (d_output < 0);
if (any_negative) {
    // 视为新 baseline，丢掉这次推送的 delta
    s.last_input = input;
    s.last_cache_creation = cc;
    s.last_cache_read = cr;
    s.last_output = output;
    s.last_seen_ms = now_ms;
    return Delta{0, 0, 0, 0, dt_ms};   // dt_ms 非零但 delta 全 0
}
```

`Tick()` 看到 `delta` 全 0 + `dt_ms>0` 不会入滑动窗口（每个 delta 都为 0），但 `last_seen_ms` 已被刷新，`ForgetInactiveSessions` 不会误清。

## 1Hz 窗口

`DataManager` 持有：

```cpp
struct RecentDelta {
    ULONGLONG ts_ms;
    long long d_input, d_cc, d_cr, d_output;
};
std::deque<RecentDelta> m_recent_deltas;
WindowSum m_window;   // {long long input, cc, cr, output}
```

`Tick()` 每秒 1 次：
```cpp
ULONGLONG now_ms = GetTickCount64();
ULONGLONG cutoff = now_ms - 1000;
while (!m_recent_deltas.empty() && m_recent_deltas.front().ts_ms < cutoff) {
    m_window.input       -= m_recent_deltas.front().d_input;
    m_window.cache_creation -= m_recent_deltas.front().d_cc;
    m_window.cache_read     -= m_recent_deltas.front().d_cr;
    m_window.output         -= m_recent_deltas.front().d_output;
    m_recent_deltas.pop_front();
}
```

每条 entry 的 delta 入队（带 `ts_ms = now_ms`）。`m_window` 始终是"过去 1 秒内的 delta 总和"。

## 归一化

每个 token 类独立归一化，基线 = 60s 滑动最大值：

```cpp
struct SlidingMax {
    ULONGLONG value{100};
    ULONGLONG ts_ms{0};
};
SlidingMax m_max_in, m_max_cc, m_max_cr, m_max_out;

float Normalize(long long speed, SlidingMax& m, ULONGLONG now_ms) {
    // 60s 滑动最大
    if (now_ms - m.ts_ms > 60000) {
        m.value = 100;  // 60s 没更新就重置
        m.ts_ms = now_ms;
    }
    if ((unsigned long long)speed > m.value) {
        m.value = (unsigned long long)speed;
        m.ts_ms = now_ms;
    }
    // floor 100 tok/s 防 0 除
    ULONGLONG baseline = max(m.value, (ULONGLONG)100);
    return (float)speed / (float)baseline;  // 0.0 ~ 1.0
}
```

`floor 100 tok/s` 的作用：刚启动时 `speed=0` 且 `m.value=0` 会触发 0 除；强制 baseline >= 100 让初始归一化结果是 0（柱形图平），待真实速度超过 100 后 baseline 才被刷新。

## 环形缓冲

`CRingBuffer<float, N=128>`，固定容量模板：

```cpp
template<typename T, size_t N>
class CRingBuffer {
    std::array<T, N> m_data{};
    size_t m_head{0};
    size_t m_size{0};
public:
    void Push(const T& v) {
        m_data[m_head] = v;
        m_head = (m_head + 1) % N;
        if (m_size < N) ++m_size;
    }
    T At(size_t i) const {   // 0=oldest, size-1=newest
        size_t idx = (m_head + N - m_size + i) % N;
        return m_data[idx];
    }
    size_t Size() const { return m_size; }
};
```

`N=128` 在 1Hz 采样率下 = 128 秒 ≈ 2 分钟历史。`DrawItem` 遍历时从 `At(0)` 到 `At(Size()-1)`，最新一帧画在最右。

每个 token 类各一个 `CRingBuffer`：
- `m_input_history`
- `m_cache_creation_history`
- `m_cache_read_history`
- `m_output_history`

## Tick() 顺序

```cpp
void DataManager::Tick() {
    ULONGLONG now_ms = GetTickCount64();
    if (m_last_tick_ms == 0) { m_last_tick_ms = now_ms; return; }

    // 1. 拉取新条目
    auto entries = m_reader.DrainNewEntries();

    // 2. 喂给 accumulator
    for (auto& e : entries) {
        if (!e.parsed_ok) continue;
        SessionKey key{e.session_id, e.cwd};
        Delta d = m_acc.OnStatuslineUpdate(key, e.input, e.output,
                                           e.cache_creation, e.cache_read,
                                           now_ms);
        if (d.dt_ms == 0) continue;          // first_seen
        if (d.input==0 && d.cc==0 && d_cr==0 && d.output==0) continue;  // 倒退重置
        m_window += {d.input, d.cc, d.d_cr, d.output};
        m_recent_deltas.push_back({now_ms, d});
    }

    // 3. 滑动窗口剔除 > 1s 前的
    ULONGLONG cutoff = now_ms - 1000;
    while (!m_recent_deltas.empty() && m_recent_deltas.front().ts_ms < cutoff) {
        m_window -= m_recent_deltas.front();
        m_recent_deltas.pop_front();
    }

    // 4. 清理闲置 session
    m_acc.ForgetInactiveSessions(now_ms, 600000);

    // 5. 按 aggregate_mode 过滤后取 1s 窗口的 sum
    long long speed_in = m_window.input;
    long long speed_cc = m_window.cache_creation;
    long long speed_cr = m_window.cache_read;
    long long speed_out = m_window.output;

    // 6. 归一化入环形缓冲
    m_input_history.Push(Normalize(speed_in, m_max_in, now_ms));
    m_cache_creation_history.Push(Normalize(speed_cc, m_max_cc, now_ms));
    m_cache_read_history.Push(Normalize(speed_cr, m_max_cr, now_ms));
    m_output_history.Push(Normalize(speed_out, m_max_out, now_ms));

    // 7. 格式化数字文本（"12.3k/s" 等）
    FormatValueText();
    m_last_tick_ms = now_ms;
}
```

注意 step 5：`speed_*` 直接取 `m_window.*`，**不再按 session 过滤**。这是设计上的简化：1Hz 窗口本身是"过去 1 秒所有 session 的总和"，对应 ALL 模式。如果用户在 ACTIVE/SINGLE 模式，m_window 仍含被过滤 session 的 delta，过滤在 step 5 的"取窗口值"之后由 `m_input_history` 的归一化侧间接体现：

实际 ACTIVE/SINGLE 模式的下游过滤放在 `FormatValueText()` 之前的 `CTokenItem` 数据路径里：每个 `CTokenItem` 从 `DataManager` 拿到的是 `m_input_history.At(Size()-1)` 的归一化值，已经基于全量窗口归一。ACTIVE/SINGLE 的过滤影响的是 `m_last_input` 等"原子化的最近 delta"，不直接改环形缓冲。

简化说明：本插件当前实现下 ACTIVE/SINGLE 模式只在 `FormatValueText()` 阶段生效（即只改数字文本的过滤，不改柱形图）。这是当前实现，不是性能目标。