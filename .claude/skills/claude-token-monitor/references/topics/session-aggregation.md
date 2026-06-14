# Session Aggregation

`DataManager` 持有 `std::unordered_map<SessionKey, SessionState, SessionKeyHash> m_acc`（`CPerSessionAccumulator`），每条 statusline 推送对应一个 session。每个 `SessionState` 存四类 token 的 `last_*` baseline 和 `last_seen_ms` / `session_start_ms` 时间戳。

本插件用 3 种 `AggregateMode` 决定 Tick 时把哪些 session 的速度加到任务栏显示。

## 三种模式

定义在 `DataManager.h`：

```cpp
enum class AggregateMode { ALL, ACTIVE, SINGLE };
struct SettingData {
    AggregateMode aggregate_mode{ AggregateMode::ALL };
    std::wstring single_session_id;
    int active_window_ms{ 60000 };
    std::vector<std::wstring> ignored_sessions;
    // ...
};
```

### ALL（默认）

所有 session 的 token 速度直接相加。`Tick()` 里：
```cpp
for (auto& [key, state] : m_acc.Snapshot()) {
    if (!ShouldAggregate(key)) continue;
    speed_in  += state.last_input;
    speed_cc  += state.last_cache_creation;
    speed_cr  += state.last_cache_read;
    speed_out += state.last_output;
}
```

`ShouldAggregate()` 在 ALL 模式只检查 `ignored_sessions`。

### ACTIVE

只统计过去 `active_window_ms`（默认 60000 = 60s）内有 statusline 更新的 session：
```cpp
bool IsActive(const SessionState& s, ULONGLONG now_ms) {
    return (now_ms - s.last_seen_ms) <= (ULONGLONG)setting.active_window_ms;
}
```

`Tick()` 过滤：
```cpp
for (auto& [key, state] : m_acc.Snapshot()) {
    if (!ShouldAggregate(key)) continue;
    if (!IsActive(state, now_ms)) continue;
    // 累加
}
```

关闭 Claude Code 60s 后该 session 自动从任务栏速度里消失，柱形图继续显示其他活跃 session。

### SINGLE

只统计用户从下拉里选中的某个 `session_id`：
```cpp
if (state.session_id != setting.single_session_id) continue;
```

用户在 `COptionsDlg` 的 `IDC_CBO_SINGLE_SESSION` 下拉里选一个 session。下拉只对 SINGLE 模式可见。

## ignored_sessions

`SettingData::ignored_sessions` 是 `std::vector<std::wstring>`，按 session_id 排除。任意模式下都生效：
```cpp
bool ShouldAggregate(const SessionKey& key) {
    if (std::find(setting.ignored_sessions.begin(),
                  setting.ignored_sessions.end(),
                  key.session_id) != setting.ignored_sessions.end())
        return false;
    // 模式相关过滤
    return true;
}
```

用户在 `COptionsDlg` 的 `IDC_LST_IGNORED` 列表里勾选要从聚合里排除的 session。列表任意模式可见。

## session 列表来源

`COptionsDlg` 的 `IDC_CBO_SINGLE_SESSION` 下拉和 `IDC_LST_IGNORED` 列表的内容来源是 `DataManager::ListRecentSessions(int lookback_ms)`：

```cpp
std::vector<SessionInfo> ListRecentSessions(int lookback_ms = 24 * 3600 * 1000) {
    auto entries = m_reader.DrainNewEntries();   // 拉取新增
    // 也读 sidecar 全量历史（不只增量），构造 session 集合
    std::unordered_map<std::wstring, SessionInfo> map;
    // 扫 sidecar.jsonl 全文，提取 (session_id, last_seen_ms) 列表
    // 过滤：last_seen_ms >= now_ms - lookback_ms
    // 按 last_seen_ms 倒序返回
}
```

`lookback_ms` 默认 24 小时（`24 * 3600 * 1000`）。侧栏列表只显示最近 24h 内出现过的 session，避免开了 5+ session 时 dialog 列表过长。

用户在 `COptionsDlg` 点 `IDC_BTN_REFRESH_SESSIONS` 按钮 → 重新调 `ListRecentSessions()` → 重填下拉和列表。

## session 失效判定

`CPerSessionAccumulator::ForgetInactiveSessions(ULONGLONG now_ms, ULONGLONG ttl_ms = 600000)`：

```cpp
void ForgetInactiveSessions(ULONGLONG now_ms, ULONGLONG ttl_ms) {
    for (auto it = m_states.begin(); it != m_states.end(); ) {
        if ((now_ms - it->second.last_seen_ms) > ttl_ms)
            it = m_states.erase(it);
        else
            ++it;
    }
}
```

TTL 默认 600000ms = 10 分钟。超过 10 分钟无 statusline 更新的 session 从 `m_acc` 移除，释放内存。

`Tick()` 每次都调 `ForgetInactiveSessions()`。失效的 session 不会出现在 `ListRecentSessions()`（因为 `last_seen_ms` 也对应更新；移除 map 后下次扫 sidecar 历史会按 `last_seen_ms` 重新发现并 rebuild）。

## SessionKey 与 SessionState

```cpp
struct SessionKey {
    std::wstring session_id;
    std::wstring cwd;
    bool operator==(const SessionKey&) const = default;
};
struct SessionKeyHash {
    size_t operator()(const SessionKey& k) const noexcept {
        return std::hash<std::wstring>{}(k.session_id) ^
               (std::hash<std::wstring>{}(k.cwd) << 1);
    }
};

struct SessionState {
    unsigned long long last_input{};
    unsigned long long last_cache_creation{};
    unsigned long long last_cache_read{};
    unsigned long long last_output{};
    ULONGLONG last_seen_ms{};
    ULONGLONG session_start_ms{};
    bool first_seen{ true };
};
```

`SessionKey` 用 `(session_id, cwd)` 二元组区分：理论上同一 `session_id` 不会跨 `cwd` 复用，但保留 `cwd` 防止 hash 碰撞误聚合。`last_seen_ms` 是本进程 `GetTickCount64()`（不是 `wrapper_ms`，避免时钟漂移）；`session_start_ms` 是首次见到该 key 的时间，用于将来扩展"按 session 显示累计"功能。`first_seen=true` 时 `OnStatuslineUpdate` 不算 delta（baseline 建立），详见 `delta-algorithm.md`。