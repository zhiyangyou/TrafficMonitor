// Accumulator.cpp — CPerSessionAccumulator: per-session baseline tracking + Delta computation.
// Reference: plan section 4.2 step 1-3; Accumulator.h struct definitions.
// Detail: .claude/skills/claude-token-monitor/references/topics/delta-algorithm.md

#include "pch.h"
#include "Accumulator.h"

Delta CPerSessionAccumulator::OnStatuslineUpdate(const SessionKey& key,
                                                unsigned long long input,
                                                unsigned long long output,
                                                unsigned long long cache_creation,
                                                unsigned long long cache_read,
                                                ULONGLONG now_ms)
{
    // Default: empty delta (dt_ms=0 signals "no delta this call").
    Delta d{};

    auto it = m_states.find(key);
    if (it == m_states.end() || it->second.first_seen)
    {
        // First time we see this key (or it was reset by backward detection).
        // Establish baseline; do NOT emit a delta (caller skips dt_ms==0).
        SessionState st;
        st.last_input = input;
        st.last_output = output;
        st.last_cache_creation = cache_creation;
        st.last_cache_read = cache_read;
        st.last_seen_ms = now_ms;
        st.session_start_ms = now_ms;
        st.first_seen = false;
        m_states[key] = st;
        return d;  // dt_ms=0
    }

    SessionState& st = it->second;

    // Backward detection: any field shrunk → /compact or reset.
    // Re-baseline silently, drop this push's delta.
    if (input < st.last_input || output < st.last_output ||
        cache_creation < st.last_cache_creation || cache_read < st.last_cache_read)
    {
        st.first_seen = true;
        st.last_input = input;
        st.last_output = output;
        st.last_cache_creation = cache_creation;
        st.last_cache_read = cache_read;
        st.last_seen_ms = now_ms;
        return d;  // dt_ms=0, all-zero delta
    }

    // Normal path: compute 4-way delta.
    d.input = static_cast<long long>(input - st.last_input);
    d.output = static_cast<long long>(output - st.last_output);
    d.cache_creation = static_cast<long long>(cache_creation - st.last_cache_creation);
    d.cache_read = static_cast<long long>(cache_read - st.last_cache_read);
    // Floor dt_ms to 1ms so downstream never divides by 0; usually 1000ms at 1Hz.
    d.dt_ms = (now_ms > st.last_seen_ms) ? (now_ms - st.last_seen_ms) : 1;

    // Update baseline.
    st.last_input = input;
    st.last_output = output;
    st.last_cache_creation = cache_creation;
    st.last_cache_read = cache_read;
    st.last_seen_ms = now_ms;
    return d;
}

void CPerSessionAccumulator::ForgetInactiveSessions(ULONGLONG now_ms, ULONGLONG ttl_ms)
{
    // Erase entries idle for longer than ttl_ms. Caller is expected to pass
    // monotonic now_ms from GetTickCount64(); guard with a >0 check.
    for (auto it = m_states.begin(); it != m_states.end(); )
    {
        if (now_ms > it->second.last_seen_ms &&
            (now_ms - it->second.last_seen_ms) > ttl_ms)
        {
            it = m_states.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::vector<std::pair<SessionKey, SessionState>> CPerSessionAccumulator::Snapshot() const
{
    // Copy map contents into a vector for stable iteration in the caller.
    std::vector<std::pair<SessionKey, SessionState>> out;
    out.reserve(m_states.size());
    for (const auto& kv : m_states)
    {
        out.emplace_back(kv.first, kv.second);
    }
    return out;
}
