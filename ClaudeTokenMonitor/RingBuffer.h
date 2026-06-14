#pragma once
#include <array>
#include <cstddef>

// CRingBuffer<T, N> — fixed-capacity ring buffer of POD values.
// Used by CDataManager for 4× normalized 0..1 history samples per token category.
// Push() overwrites oldest when full. At(i) is 0=oldest, Size()-1=newest.
// Reference: plan §4.2 step 6; PluginDemo doesn't have an equivalent (new here).
// TODO detail: .claude/skills/claude-token-monitor/references/topics/custom-draw.md (bar drawing iterates At(0..Size()-1))
template <typename T, std::size_t N>
class CRingBuffer
{
public:
    CRingBuffer() : m_head(0), m_count(0) {}

    void Push(const T& value)
    {
        m_data[m_head] = value;
        m_head = (m_head + 1) % N;
        if (m_count < N) ++m_count;
    }

    // 0 = oldest, Size()-1 = newest
    T At(std::size_t i) const
    {
        // i in [0, m_count); start at (m_head - m_count + N) % N
        std::size_t start = (m_head + N - m_count) % N;
        return m_data[(start + i) % N];
    }

    std::size_t Size() const { return m_count; }

    static constexpr std::size_t Capacity() { return N; }

    void Clear() { m_head = 0; m_count = 0; }

private:
    std::array<T, N> m_data{};
    std::size_t m_head;
    std::size_t m_count;
};