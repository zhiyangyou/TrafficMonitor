#pragma once
#include "pch.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <map>

#include "Accumulator.h"
#include "RingBuffer.h"
#include "SidecarReader.h"
#include "TokenItem.h"  // for TokenCategory

// AggregateMode — how CDataManager::Tick() aggregates tokens across sessions.
// Reference: plan §3 DataManager.h; 01-context.md §8
enum class AggregateMode
{
    ALL,        // sum all sessions (minus ignored_sessions)
    ACTIVE,     // sum only sessions seen within active_window_ms
    SINGLE      // sum only the session matching single_session_id
};

// SettingData — persisted plugin config (read/written via <config_dir>\ClaudeTokenMonitor.dll.ini).
// Reference: plan §3 DataManager.h; PluginDemo/DataManager.cpp:27-35 (INI persistence pattern)
struct SettingData
{
    AggregateMode aggregate_mode{ AggregateMode::ALL };
    std::wstring single_session_id;
    int active_window_ms{ 60000 };
    std::vector<std::wstring> ignored_sessions;
    COLORREF color_input{ RGB(0, 200, 80) };
    COLORREF color_cache_creation{ RGB(230, 180, 0) };
    COLORREF color_cache_read{ RGB(80, 140, 230) };
    COLORREF color_output{ RGB(180, 80, 220) };
    int refresh_interval_ms{ 1000 };
    std::wstring wrapper_path_override;
};

// History capacity — 60 samples at 1Hz = 60s rolling window.
static constexpr std::size_t kHistoryCapacity = 60;

// CDataManager — singleton holding all runtime state.
// Reference: plan §2 / §4.2; 02-architecture.md §2.1
// TODO detail: .claude/skills/claude-token-monitor/references/topics/session-aggregation.md
class CDataManager
{
private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    // Load plugin config from <config_dir>\ClaudeTokenMonitor.dll.ini
    // Reference: PluginDemo/DataManager.cpp:20-38
    void LoadConfig(const std::wstring& config_dir);

    // Save plugin config back to m_config_path.
    // Reference: PluginDemo/DataManager.cpp:47-51
    void SaveConfig() const;

    // String resource cache (i18n via String Table).
    // Reference: PluginDemo/DataManager.cpp:53-66
    const CString& StringRes(UINT id);

    // Save ITrafficMonitor* + config_dir for the throttled ini re-patcher
    // in Tick(). See TaskbarItemRegistrar.h / plan section 5.
    void SetRegistrarContext(ITrafficMonitor* pApp, const std::wstring& config_dir)
    {
        m_app_for_registrar = pApp;
        m_cached_config_dir = config_dir;
    }

    // 1Hz main loop. Called by CClaudeTokenMonitorPlugin::DataRequired().
    // See plan §4.2 for the full Tick() pseudo-code.
    // TODO: implement per plan §4.2 / 03-data-flow.md §跳 4-7
    void Tick();

    // Reset runtime state (m_acc + m_reader + window). Called by OnInitialize
    // to prevent stale delta after FreeLibrary+LoadLibrary reload.
    // See: plan §7 risk table; references/topics/risk-and-edge-cases.md
    void ResetRuntimeState();

    // Getter for ring-buffer normalized values + value text + color per category.
    // Used by CTokenItem::DrawItem (called from UI thread).
    float GetGraphValue(TokenCategory cat) const;
    const CString& GetValueText(TokenCategory cat) const;
    const CString& GetValueSampleText(TokenCategory cat) const;
    COLORREF GetItemColor(TokenCategory cat) const;

    // Read-only accessor for the ring buffer of a given token category.
    // CTokenItem::DrawItem uses this to walk the history and draw bars.
    const CRingBuffer<float, kHistoryCapacity>& GetGraphHistory(TokenCategory cat) const;

    // Settings accessor (used by COptionsDlg).
    SettingData& Settings() { return m_setting_data; }
    const SettingData& Settings() const { return m_setting_data; }

    // Path accessors (delegated to CStatuslineInstaller).
    std::wstring GetSidecarPath() const;
    std::wstring GetWrapperPath() const;

    // Callback hook (fired when active session list changes; COptionsDlg refresh button).
    // TODO: implement observer pattern (std::function<void()>); called from Tick().
    using SessionListChangedCallback = std::function<void()>;
    void SetSessionListChangedCallback(SessionListChangedCallback cb) { m_on_session_list_changed = std::move(cb); }

    // Active session list snapshot (session_id, last_seen_ms, model) — sorted desc by last_seen.
    // TODO: derive from m_acc.Snapshot(); trim to last 24h; respect ignored_sessions.
    struct SessionInfo
    {
        std::wstring session_id;
        std::wstring cwd;
        std::wstring model;
        ULONGLONG last_seen_ms{};
    };
    std::vector<SessionInfo> GetActiveSessions() const;

private:
    static CDataManager m_instance;

    SettingData m_setting_data;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;

    // Runtime state (set by Tick()).
    CPerSessionAccumulator m_acc;
    CSidecarReader m_reader;
    CRingBuffer<float, kHistoryCapacity> m_input_history;
    CRingBuffer<float, kHistoryCapacity> m_cache_write_history;
    CRingBuffer<float, kHistoryCapacity> m_cache_read_history;
    CRingBuffer<float, kHistoryCapacity> m_output_history;

    // Throttled ini re-patcher context (set by CClaudeTokenMonitorPlugin::OnInitialize).
    // DataManager::Tick() calls CTaskbarItemRegistrar::EnsureRegisteredThrottled
    // every 5s to recover from main program's SaveConfig() clobbering the ini.
    ITrafficMonitor* m_app_for_registrar{};
    std::wstring m_cached_config_dir;

    // Last 1s window deltas (sliding).
    struct { long long input{}, cache_creation{}, cache_read{}, output{}; } m_window{};
    ULONGLONG m_last_tick_ms{};

    // Per-tick deltas with timestamps. Used to slide the 1s window: pop entries
    // older than (now_ms - 1000). deque<...> gives O(1) pop_front.
    struct RecentDelta
    {
        ULONGLONG ts_ms{};
        Delta d{};
    };
    std::deque<RecentDelta> m_recent_deltas;

    // Sliding 60s maximum per token category, used as the normalization baseline.
    // floor of 100 tok/s prevents divide-by-zero at startup.
    struct SlidingMax
    {
        unsigned long long value{ 100 };
        ULONGLONG ts_ms{ 0 };
    };
    SlidingMax m_max_in;
    SlidingMax m_max_cc;
    SlidingMax m_max_cr;
    SlidingMax m_max_out;

    // Value text cache — formatted by Tick(), read by CTokenItem::GetItemValueText.
    CString m_input_value_text;
    CString m_cache_write_value_text;
    CString m_cache_read_value_text;
    CString m_output_value_text;

    // Optional observer for COptionsDlg refresh.
    SessionListChangedCallback m_on_session_list_changed;
};