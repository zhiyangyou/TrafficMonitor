// DataManager.cpp — singleton, INI load/save, Tick() skeleton, value-text formatting.
// Reference: plan §4.2 Tick(); PluginDemo/DataManager.cpp
// TODO detail: .claude/skills/claude-token-monitor/references/topics/session-aggregation.md

#include "pch.h"
#include "DataManager.h"
#include "StatuslineInstaller.h"

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
    // See: plan §3 DataManager.h SettingData struct; references/topics/session-aggregation.md
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
    // See: plan §3 SettingData struct.
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

void CDataManager::Tick()
{
    // Reference: plan §4.2 Tick() pseudo-code (6 steps).
    // TODO: full implementation:
    //   1. now_ms = GetTickCount64()
    //      if m_last_tick_ms == 0: m_last_tick_ms = now_ms; return (first-frame baseline).
    //   2. entries = m_reader.DrainNewEntries()
    //      for e in entries (skip if !e.parsed_ok):
    //        key = {e.session_id, e.cwd}
    //        d = m_acc.OnStatuslineUpdate(key, e.input, e.output, e.cache_creation, e.cache_read, now_ms)
    //        if d.dt_ms == 0: continue  // first_seen
    //        m_window += d.{input,cache_creation,cache_read,output}
    //   3. Slide 1s window: pop deltas older than now_ms-1000 from a deque of {ts_ms, Delta}.
    //   4. m_acc.ForgetInactiveSessions(now_ms, 600000)
    //   5. ShouldAggregate(key) filter (ALL/ACTIVE/SINGLE) over m_acc.Snapshot() for m_max_* tracking.
    //   6. Normalize m_window.{input,cc,cr,output} by m_max_*.{...} (floor 100 tok/s) →
    //      m_input_history.Push(...) etc.
    //   7. FormatValueText(): "12.3k/s" → m_input_value_text etc.
    //   8. m_last_tick_ms = now_ms
    // See: plan §4.2; 03-data-flow.md §跳 7; references/topics/delta-algorithm.md

    // TODO: rotate sidecar if > 5MB (close, rename to .old, reopen, m_reader.ResetOffset()).
    // See: plan §7 risk table.
}

void CDataManager::ResetRuntimeState()
{
    m_acc.Reset();
    m_reader.ResetOffset();
    m_window = {};
    m_last_tick_ms = 0;
    m_input_history.Clear();
    m_cache_write_history.Clear();
    m_cache_read_history.Clear();
    m_output_history.Clear();
}

float CDataManager::GetGraphValue(TokenCategory cat) const
{
    // TODO: return ring buffer most-recent normalized value (At(Size()-1)).
    // Reference: CTokenItem::DrawItem (plan §4.3 step 8-9).
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
    std::vector<SessionInfo> out;
    // TODO: iterate m_acc.Snapshot(); build SessionInfo; sort by last_seen_ms desc;
    //       trim to last 24h; respect ignored_sessions (blacklist).
    // Reference: plan §6 options dialog §刷新 sessions 按钮
    return out;
}