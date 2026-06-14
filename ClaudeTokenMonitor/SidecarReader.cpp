// SidecarReader.cpp — CJsonlDirectoryWatcher impl (post-ADR-0005).
// Watches Claude Code project JSONL files, deduplicates by message.id, emits
// one StatuslineEntry per finalized assistant turn.
// Reference: ADR 0005 jsonl-data-source.md; references/topics/validation-flow.md

#include "pch.h"
#include "SidecarReader.h"
#include <nlohmann/json.hpp>
#include <string>
#include <set>

//------------------------------------------------------------------------------
// Construction
//------------------------------------------------------------------------------

CJsonlDirectoryWatcher::CJsonlDirectoryWatcher()
{
    // Default roots: ~/.claude/projects (CLI). Desktop local-agent-mode
    // is added when the env var %APPDATA%\Claude\local-agent-mode-sessions
    // exists (future v1.1).
    wchar_t profile[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::wstring root = std::wstring(profile) + L"\\.claude\\projects";
        m_roots.push_back(root);
    }
}

CJsonlDirectoryWatcher::~CJsonlDirectoryWatcher()
{
    // No persistent file handles — each ReadOneFile opens and closes.
}

//------------------------------------------------------------------------------
// Watch set refresh
//------------------------------------------------------------------------------

namespace {

// Recursively enumerate .jsonl files under `root`. Adds each to `out`.
void EnumerateJsonlFiles(const std::wstring& root, std::set<std::wstring>& out)
{
    if (root.empty()) return;

    // Find first file matching "*.jsonl" anywhere under root. We use the
    // double-globbing trick (FindFirstFileW doesn't support recursion
    // natively) by manually walking each directory level.
    std::wstring pattern = root + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do
    {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = root + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            EnumerateJsonlFiles(full, out);
        }
        else if (name.size() > 5 &&
                 name.substr(name.size() - 5) == L".jsonl")
        {
            out.insert(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

} // namespace

void CJsonlDirectoryWatcher::RefreshWatchSet()
{
    std::set<std::wstring> found;
    for (const auto& root : m_roots)
    {
        EnumerateJsonlFiles(root, found);
    }
    // Add new files (preserve existing per-file state for files we already
    // knew about; new files start at offset 0 so we ingest all history once).
    for (const auto& path : found)
    {
        m_watched_files.insert(path);
    }
    // Files that disappeared (e.g. user deleted an old session) — keep their
    // state in m_last_offsets so we don't re-ingest if the file reappears,
    // but drop the dedupe state to free memory.
    for (auto it = m_seen.begin(); it != m_seen.end(); )
    {
        if (found.find(it->first) == found.end())
        {
            it = m_seen.erase(it);
            m_emitted.erase(it->first);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = m_last_offsets.begin(); it != m_last_offsets.end(); )
    {
        if (found.find(it->first) == found.end())
        {
            it = m_last_offsets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

//------------------------------------------------------------------------------
// Single-file read with dedupe-by-msg_id
//------------------------------------------------------------------------------

HANDLE CJsonlDirectoryWatcher::OpenForRead(const std::wstring& path)
{
    return CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,            // do not create; RefreshWatchSet discovers new files
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

namespace {

// Parse one JSONL line and (if it is an `assistant` message with usage)
// populate `usage`. Returns true on success.
// Accesses CJsonlDirectoryWatcher's private FinalUsage via friend declaration
// in the .h file would normally be needed, but here we use a local POD struct
// to keep things simple and side-step the access check.
struct LocalUsage
{
    unsigned long long input{};
    unsigned long long output{};
    unsigned long long cache_creation{};
    unsigned long long cache_read{};
};
bool ParseAssistantLine(const std::string& line, std::string& msg_id_out, LocalUsage& usage_out)
{
    try
    {
        auto j = nlohmann::json::parse(line);
        if (!j.contains("type") || j["type"] != "assistant") return false;
        if (!j.contains("message") || !j["message"].is_object()) return false;
        const auto& m = j["message"];
        if (!m.contains("id") || !m["id"].is_string()) return false;
        if (!m.contains("usage") || !m["usage"].is_object()) return false;
        const auto& u = m["usage"];
        msg_id_out = m["id"].get<std::string>();
        usage_out.input          = u.value("input_tokens", 0ULL);
        usage_out.output         = u.value("output_tokens", 0ULL);
        usage_out.cache_creation = u.value("cache_creation_input_tokens", 0ULL);
        usage_out.cache_read     = u.value("cache_read_input_tokens", 0ULL);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// Derive a session_id from the JSONL file path.
// E.g. ".../projects/C--Users-YOU--claude/28014b1c-6638-4ffa-be0d-f675ea1fda8b.jsonl"
//   -> "C--Users-YOU--claude/28014b1c-6638-4ffa-be0d-f675ea1fda8b"
std::wstring DeriveSessionIdFromPath(const std::wstring& file_path)
{
    // Find ".claude\projects\" (or "projects\") and take everything after.
    const std::wstring marker1 = L"\\.claude\\projects\\";
    const std::wstring marker2 = L"\\projects\\";
    size_t pos = file_path.find(marker1);
    if (pos == std::wstring::npos) pos = file_path.find(marker2);
    if (pos == std::wstring::npos)
    {
        // Fallback: just use the filename.
        size_t slash = file_path.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? file_path : file_path.substr(slash + 1);
    }
    return file_path.substr(pos + marker1.size());
}

} // namespace

std::vector<StatuslineEntry> CJsonlDirectoryWatcher::ReadOneFile(const std::wstring& path)
{
    std::vector<StatuslineEntry> entries;
    HANDLE h = OpenForRead(path);
    if (h == INVALID_HANDLE_VALUE) return entries;

    // Query EOF and seek to m_last_offset.
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(h, &fileSize))
    {
        CloseHandle(h);
        return entries;
    }
    uint64_t eof = static_cast<uint64_t>(fileSize.QuadPart);
    uint64_t& off = m_last_offsets[path];

    // Truncation: file got smaller since we last read. Reset to 0.
    if (off > eof)
    {
        off = 0;
        m_seen[path].clear();
        m_emitted[path].clear();
    }
    if (off >= eof)
    {
        CloseHandle(h);
        return entries;
    }

    LARGE_INTEGER li_off{};
    li_off.QuadPart = static_cast<LONGLONG>(off);
    SetFilePointerEx(h, li_off, nullptr, FILE_BEGIN);

    // Read everything from off to eof in 8KB chunks.
    std::string buf;
    char chunk[8192];
    for (;;)
    {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(h, chunk, sizeof(chunk), &bytesRead, nullptr);
        if (!ok || bytesRead == 0) break;
        buf.append(chunk, bytesRead);
        off += bytesRead;
    }
    CloseHandle(h);

    // Split on '\n', strip trailing '\r'.
    const ULONGLONG now_ms = GetTickCount64();
    auto& seen_map  = m_seen[path];
    auto& emitted_set = m_emitted[path];

    size_t start = 0;
    for (;;)
    {
        size_t pos = buf.find('\n', start);
        std::string line;
        if (pos == std::string::npos)
        {
            // Trailing partial line (no \n yet) — drop it; next call will
            // re-read from the same offset and concatenate. For simplicity
            // and safety we *rewind* m_last_offsets so the next call sees
            // the same bytes. (Statusline source used m_residual_buffer;
            // here we take the simpler approach: don't advance on partial.)
            if (start < buf.size())
            {
                off -= (buf.size() - start);
            }
            break;
        }
        line = buf.substr(start, pos - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = pos + 1;

        std::string msg_id;
        LocalUsage usage;
        if (!ParseAssistantLine(line, msg_id, usage)) continue;

        // Widen UTF-8 msg_id to wstring for use as std::map key.
        const std::wstring wid(msg_id.begin(), msg_id.end());

        // Dedupe: store latest usage for this msg_id. If we already emitted
        // this msg_id, skip (we only emit ONCE per finalized msg_id).
        auto it = seen_map.find(wid);
        if (it != seen_map.end())
        {
            // Streaming duplicate (same msg_id, different line). Update.
            it->second.input          = usage.input;
            it->second.output         = usage.output;
            it->second.cache_creation = usage.cache_creation;
            it->second.cache_read     = usage.cache_read;
            // If we already emitted this msg_id, don't re-emit; otherwise
            // it's the final update — emit now.
            if (emitted_set.count(wid)) continue;
        }
        else
        {
            seen_map[wid].input          = usage.input;
            seen_map[wid].output         = usage.output;
            seen_map[wid].cache_creation = usage.cache_creation;
            seen_map[wid].cache_read     = usage.cache_read;
        }

        // First time seeing this finalized value. Emit a StatuslineEntry.
        StatuslineEntry e;
        e.session_id = DeriveSessionIdFromPath(path);
        e.input = usage.input;
        e.output = usage.output;
        e.cache_creation = usage.cache_creation;
        e.cache_read = usage.cache_read;
        e.received_ms = now_ms;
        e.parsed_ok = true;
        emitted_set.insert(wid);
        entries.push_back(e);
    }

    return entries;
}

//------------------------------------------------------------------------------
// Public drain: refresh watch set, then drain each watched file.
//------------------------------------------------------------------------------

void CJsonlDirectoryWatcher::Reset()
{
    m_last_offsets.clear();
    m_seen.clear();
    m_emitted.clear();
    m_watched_files.clear();
}

std::vector<StatuslineEntry> CJsonlDirectoryWatcher::DrainNewEntries()
{
    RefreshWatchSet();

    std::vector<StatuslineEntry> entries;
    for (const auto& path : m_watched_files)
    {
        auto file_entries = ReadOneFile(path);
        for (auto& e : file_entries)
        {
            entries.push_back(std::move(e));
        }
    }
    return entries;
}
