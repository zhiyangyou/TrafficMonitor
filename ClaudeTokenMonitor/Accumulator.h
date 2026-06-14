#pragma once
#include "pch.h"
#include <map>
#include <string>
#include <vector>
#include <utility>

// CPerSessionAccumulator — maintains per-session last-seen token counts
// and produces Delta on each statusline update.
// Reference: plan §3 struct definitions; 03-data-flow.md §跳 6
// TODO detail: .claude/skills/claude-token-monitor/references/topics/delta-algorithm.md
struct SessionKey
{
    std::wstring session_id;
    std::wstring cwd;
    bool operator==(const SessionKey& other) const
    {
        return session_id == other.session_id && cwd == other.cwd;
    }
    bool operator<(const SessionKey& other) const
    {
        if (session_id != other.session_id) return session_id < other.session_id;
        return cwd < other.cwd;
    }
};

struct SessionState
{
    unsigned long long last_input{};
    unsigned long long last_cache_creation{};
    unsigned long long last_cache_read{};
    unsigned long long last_output{};
    ULONGLONG last_seen_ms{};
    ULONGLONG session_start_ms{};
    bool first_seen{ true };
};

struct Delta
{
    long long input{};
    long long cache_creation{};
    long long cache_read{};
    long long output{};
    ULONGLONG dt_ms{};
};

// CPerSessionAccumulator — owns std::map<SessionKey, SessionState>
// On first statusline update for a given key: first_seen=true → returns Delta with dt_ms=0
// On subsequent updates: returns Delta{new - old for each token field, dt_ms = now - last_seen_ms}
// Reference: plan §4.2 step 1-3
class CPerSessionAccumulator
{
public:
    CPerSessionAccumulator() = default;

    // Called once per StatuslineEntry from CDataManager::Tick().
    // Returns the Delta produced; if key not seen before, delta.dt_ms==0 (skip in Tick()).
    Delta OnStatuslineUpdate(const SessionKey& key,
                             unsigned long long input,
                             unsigned long long output,
                             unsigned long long cache_creation,
                             unsigned long long cache_read,
                             ULONGLONG now_ms);

    // Remove sessions not seen within ttl_ms of now_ms.
    void ForgetInactiveSessions(ULONGLONG now_ms, ULONGLONG ttl_ms);

    // Snapshot of all currently-tracked sessions (key + state).
    std::vector<std::pair<SessionKey, SessionState>> Snapshot() const;

    // Clear all state (called from CClaudeTokenMonitorPlugin::OnInitialize
    // to prevent stale delta after FreeLibrary+LoadLibrary).
    // TODO: detail references/risk-and-edge-cases.md
    void Reset() { m_states.clear(); }

private:
    std::map<SessionKey, SessionState> m_states;
};