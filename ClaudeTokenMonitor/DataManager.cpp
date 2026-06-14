// DataManager.cpp — singleton, INI load/save, Tick() main loop, value-text formatting.
// Reference: plan section 4.2 Tick(); PluginDemo/DataManager.cpp

#include "pch.h"
#include "DataManager.h"
#include "StatuslineInstaller.h"
#include "TaskbarItemRegistrar.h"
#include <algorithm>

CDataManager CDataManager::m_instance;

CDataManager::CDataManager()
{
}

CDataManager::~CDataManager()
{
    SaveConfig();
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

// ImageBase helper (same trick as PluginDemo) — gives HMODULE for this DLL.
static HMODULE GetCurrentModule()
{
    return reinterpret_cast<HMODULE>(&__ImageBase);
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    // Reference: PluginDemo/DataManager.cpp:20-38
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(GetCurrentModule(), path, MAX_PATH);
    std::wstring module_path = path;
    m_config_path = module_path;
    if (!config_dir.empty())
    {
        size_t index = module_path.find_last_of(L"\\/");
        std::wstring module_file_name = module_path.substr(index + 1);
        m_config_path = config_dir + module_file_name;
    }
    m_config_path += L".ini";

    // TODO: read all SettingData fields via GetPrivateProfileInt / GetPrivateProfileString
    //   aggregate_mode, single_session_id, active_window_ms, color_*, refresh_interval_ms,
    //   wrapper_path_override. ignored_sessions needs special parsing (comma/semicolon list).
    // Default values are already set in SettingData{} initializer.
    // See: plan section 3 DataManager.h SettingData struct; references/topics/session-aggregation.md
}

static void WritePrivateProfileInt(const wchar_t* app_name, const wchar_t* key_name, int value, const wchar_t* file_path)
{
    wchar_t buff[16];
    swprintf_s(buff, L"%d", value);
    WritePrivateProfileString(app_name, key_name, buff, file_path);
}

void CDataManager::SaveConfig() const
{
    // TODO: write all SettingData fields to m_config_path via WritePrivateProfile* helpers.
    // See: plan section 3 SettingData struct.
    // Reference: PluginDemo/DataManager.cpp:47-51
}

const CString& CDataManager::StringRes(UINT id)
{
    // Reference: PluginDemo/DataManager.cpp:53-66
    auto iter = m_string_table.find(id);
    if (iter != m_string_table.end())
    {
        return iter->second;
    }
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    m_string_table[id].LoadString(id);
    return m_string_table[id];
}

namespace
{
    // Format a per-second token count as a compact text (e.g. "12.3k/s", "850/s").
    CString FormatSpeed(long long v)
    {
        CString s;
        if (v < 0) v = 0;
        if (v >= 1000000)
        {
            s.Format(L"%.1fM/s", v / 1000000.0);
        }
        else if (v >= 1000)
        {
            s.Format(L"%.1fk/s", v / 1000.0);
        }
        else
        {
            s.Format(L"%lld/s", v);
        }
        return s;
    }
}

void CDataManager::Tick()
{
    // Reference: plan section 4.2 Tick() pseudo-code; references/topics/delta-algorithm.md
    ULONGLONG now_ms = GetTickCount64();

    // 0. Throttled re-patch of main program's config.ini plugin_display_item.
    //    Required because the main program's SaveConfig() clobbers our patches
    //    every time the user changes Options. Re-applying every 5s is safe and
    //    idempotent. See TaskbarItemRegistrar.h / plan §5.
    if (m_app_for_registrar != nullptr && !m_cached_config_dir.empty())
    {
        CTaskbarItemRegistrar::EnsureRegisteredThrottled(
            m_cached_config_dir, m_app_for_registrar, 5000);
    }

    // First frame: just establish baseline timestamp, return.
    if (m_last_tick_ms == 0)
    {
        m_last_tick_ms = now_ms;
        return;
    }

    // 1. Pull new entries from sidecar.
    std::vector<StatuslineEntry> entries = m_reader.DrainNewEntries();

    // 2. For each entry, ask accumulator for delta; accumulate non-empty deltas.
    for (auto& e : entries)
    {
        if (!e.parsed_ok) continue;

        // Skip sessions the user explicitly ignored.
        bool is_ignored = std::find(m_setting_data.ignored_sessions.begin(),
                                    m_setting_data.ignored_sessions.end(),
                                    e.session_id) != m_setting_data.ignored_sessions.end();
        if (is_ignored) continue;

        SessionKey key{ e.session_id, e.cwd };
        Delta d = m_acc.OnStatuslineUpdate(key,
                                          e.input, e.output,
                                          e.cache_creation, e.cache_read,
                                          now_ms);

        // dt_ms==0 → first_seen or backward-reset baseline. Skip.
        if (d.dt_ms == 0) continue;

        m_window.input         += d.input;
        m_window.cache_creation += d.cache_creation;
        m_window.cache_read    += d.cache_read;
        m_window.output        += d.output;

        m_recent_deltas.push_back({ now_ms, d });
    }

    // 3. Slide 1s window: drop entries older than (now_ms - 1000).
    {
        ULONGLONG cutoff = (now_ms > 1000ULL) ? (now_ms - 1000ULL) : 0ULL;
        while (!m_recent_deltas.empty() && m_recent_deltas.front().ts_ms < cutoff)
        {
            const RecentDelta& old = m_recent_deltas.front();
            m_window.input         -= old.d.input;
            m_window.cache_creation -= old.d.cache_creation;
            m_window.cache_read    -= old.d.cache_read;
            m_window.output        -= old.d.output;
            m_recent_deltas.pop_front();
        }
    }
    // Defensive clamp: subtraction could theoretically go negative if
    // a delta landed in m_recent_deltas after the cutoff was computed.
    if (m_window.input < 0)         m_window.input = 0;
    if (m_window.cache_creation < 0) m_window.cache_creation = 0;
    if (m_window.cache_read < 0)    m_window.cache_read = 0;
    if (m_window.output < 0)        m_window.output = 0;

    // 4. Reap sessions idle for > 10 minutes.
    m_acc.ForgetInactiveSessions(now_ms, 600000ULL);

    // 5. (Aggregation filtering is a v1 simplification: the 1s window itself
    //    already contains the sum of all non-ignored sessions. ALL/ACTIVE/SINGLE
    //    modes only affect FormatValueText at the moment. The 60s sliding-max
    //    tracker still updates from the all-session sum.)

    // 6. Update 60s sliding max → normalization baseline.
    //    If no sample in the last 60s, reset to the seed floor (100 tok/s) to
    //    prevent stale spikes from dominating the normalization.
    auto update_max = [&](SlidingMax& sm, unsigned long long v)
    {
        if (now_ms > sm.ts_ms && (now_ms - sm.ts_ms) > 60000ULL)
        {
            sm.value = 100;
            sm.ts_ms = now_ms;
        }
        if (v > sm.value)
        {
            sm.value = v;
            sm.ts_ms = now_ms;
        }
    };
    update_max(m_max_in,  static_cast<unsigned long long>(m_window.input));
    update_max(m_max_cc,  static_cast<unsigned long long>(m_window.cache_creation));
    update_max(m_max_cr,  static_cast<unsigned long long>(m_window.cache_read));
    update_max(m_max_out, static_cast<unsigned long long>(m_window.output));

    // 7. Normalize each 1s sum into [0, 1] and push to its ring buffer.
    auto normalize = [](long long v, unsigned long long max_v) -> float
    {
        unsigned long long baseline = (max_v > 100ULL) ? max_v : 100ULL;
        double ratio = static_cast<double>(v) / static_cast<double>(baseline);
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        return static_cast<float>(ratio);
    };
    m_input_history.Push(normalize(m_window.input,         m_max_in.value));
    m_cache_write_history.Push(normalize(m_window.cache_creation, m_max_cc.value));
    m_cache_read_history.Push(normalize(m_window.cache_read,    m_max_cr.value));
    m_output_history.Push(normalize(m_window.output,        m_max_out.value));

    // 8. Format value text for the 4 right-side labels.
    m_input_value_text      = FormatSpeed(m_window.input);
    m_cache_write_value_text = FormatSpeed(m_window.cache_creation);
    m_cache_read_value_text  = FormatSpeed(m_window.cache_read);
    m_output_value_text     = FormatSpeed(m_window.output);

    m_last_tick_ms = now_ms;

    // TODO: rotate sidecar if > 5MB (close, rename to .old, reopen, m_reader.ResetOffset()).
    // See: plan section 7 risk table.
}

void CDataManager::ResetRuntimeState()
{
    m_acc.Reset();
    m_reader.ResetOffset();
    m_window = {};
    m_last_tick_ms = 0;
    m_recent_deltas.clear();
    m_max_in = {};
    m_max_cc = {};
    m_max_cr = {};
    m_max_out = {};
    m_input_history.Clear();
    m_cache_write_history.Clear();
    m_cache_read_history.Clear();
    m_output_history.Clear();
    m_input_value_text.Empty();
    m_cache_write_value_text.Empty();
    m_cache_read_value_text.Empty();
    m_output_value_text.Empty();
}

float CDataManager::GetGraphValue(TokenCategory cat) const
{
    // Most-recent normalized value (At(Size()-1)).
    const CRingBuffer<float, kHistoryCapacity>* buf = nullptr;
    switch (cat)
    {
    case TokenCategory::Input:      buf = &m_input_history; break;
    case TokenCategory::CacheWrite: buf = &m_cache_write_history; break;
    case TokenCategory::CacheRead:  buf = &m_cache_read_history; break;
    case TokenCategory::Output:     buf = &m_output_history; break;
    }
    if (!buf || buf->Size() == 0) return 0.0f;
    return buf->At(buf->Size() - 1);
}

const CRingBuffer<float, kHistoryCapacity>& CDataManager::GetGraphHistory(TokenCategory cat) const
{
    // Static dummy: needed to anchor the return type's storage. Switch dispatches
    // to the correct member ring buffer. The reference is valid for the lifetime
    // of the singleton, so callers can iterate freely.
    static const CRingBuffer<float, kHistoryCapacity> kEmpty{};
    switch (cat)
    {
    case TokenCategory::Input:      return m_input_history;
    case TokenCategory::CacheWrite: return m_cache_write_history;
    case TokenCategory::CacheRead:  return m_cache_read_history;
    case TokenCategory::Output:     return m_output_history;
    }
    return kEmpty;
}

const CString& CDataManager::GetValueText(TokenCategory cat) const
{
    switch (cat)
    {
    case TokenCategory::Input:      return m_input_value_text;
    case TokenCategory::CacheWrite: return m_cache_write_value_text;
    case TokenCategory::CacheRead:  return m_cache_read_value_text;
    case TokenCategory::Output:     return m_output_value_text;
    }
    static const CString empty;
    return empty;
}

const CString& CDataManager::GetValueSampleText(TokenCategory cat) const
{
    // Used by IPluginItem::GetItemValueSampleText() for width calculation.
    // Returns a fixed sample (e.g. "999k/s") so the column reserves enough width.
    // TODO: cache these 4 strings in a static array; or rely on CTokenItem's constructor sample.
    static const CString sample = L"999k/s";
    return sample;
}

COLORREF CDataManager::GetItemColor(TokenCategory cat) const
{
    switch (cat)
    {
    case TokenCategory::Input:      return m_setting_data.color_input;
    case TokenCategory::CacheWrite: return m_setting_data.color_cache_creation;
    case TokenCategory::CacheRead:  return m_setting_data.color_cache_read;
    case TokenCategory::Output:     return m_setting_data.color_output;
    }
    return RGB(255, 255, 255);
}

std::wstring CDataManager::GetSidecarPath() const
{
    return CStatuslineInstaller::GetSidecarPath();
}

std::wstring CDataManager::GetWrapperPath() const
{
    if (!m_setting_data.wrapper_path_override.empty())
        return m_setting_data.wrapper_path_override;
    return CStatuslineInstaller::GetWrapperPath();
}

std::vector<CDataManager::SessionInfo> CDataManager::GetActiveSessions() const
{
    // Build list from the accumulator's current state (already in memory after
    // at least one Tick()). Filter to 24h lookback; sort by last_seen_ms desc.
    std::vector<SessionInfo> out;
    auto snapshot = m_acc.Snapshot();
    ULONGLONG now = GetTickCount64();
    constexpr ULONGLONG k24hMs = 24ULL * 60ULL * 60ULL * 1000ULL;

    out.reserve(snapshot.size());
    for (const auto& kv : snapshot)
    {
        const auto& key = kv.first;
        const auto& state = kv.second;
        if (now < state.last_seen_ms) continue;
        if ((now - state.last_seen_ms) > k24hMs) continue;

        SessionInfo si;
        si.session_id   = key.session_id;
        si.cwd          = key.cwd;
        si.last_seen_ms = state.last_seen_ms;
        out.push_back(std::move(si));
    }

    std::sort(out.begin(), out.end(),
              [](const SessionInfo& a, const SessionInfo& b)
              {
                  return a.last_seen_ms > b.last_seen_ms;
              });
    return out;
}
