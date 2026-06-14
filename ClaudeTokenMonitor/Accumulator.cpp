// Accumulator.cpp — CPerSessionAccumulator method skeletons.
// Reference: plan §4.2 step 1-3; Accumulator.h struct definitions.
// TODO detail: .claude/skills/claude-token-monitor/references/topics/delta-algorithm.md

#include "pch.h"
#include "Accumulator.h"

Delta CPerSessionAccumulator::OnStatuslineUpdate(const SessionKey& key,
                                                unsigned long long input,
                                                unsigned long long output,
                                                unsigned long long cache_creation,
                                                unsigned long long cache_read,
                                                ULONGLONG now_ms)
{
    Delta d;
    // TODO: if key not in m_states → insert SessionState{first_seen=true, last_*=input/output/cc/cr,
    //       last_seen_ms=now_ms, session_start_ms=now_ms}; return d (dt_ms=0).
    // TODO: else → d.input = input - state.last_input (cast to long long); same for cache_creation,
    //       cache_read, output; d.dt_ms = now_ms - state.last_seen_ms;
    //       update state.last_* and state.last_seen_ms; clear first_seen.
    // See: plan §4.2 step 1-3; references/topics/delta-algorithm.md
    return d;
}

void CPerSessionAccumulator::ForgetInactiveSessions(ULONGLONG now_ms, ULONGLONG ttl_ms)
{
    // TODO: iterate m_states; erase entries where now_ms - state.last_seen_ms > ttl_ms
    // See: plan §4.2 step 4; references/topics/delta-algorithm.md (TTL = 600000ms default)
}

std::vector<std::pair<SessionKey, SessionState>> CPerSessionAccumulator::Snapshot() const
{
    std::vector<std::pair<SessionKey, SessionState>> out;
    // TODO: copy m_states into out vector
    // Used by CDataManager::Tick() step 5 to iterate per-session last_* values.
    return out;
}