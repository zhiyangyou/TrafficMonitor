#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <windows.h>

// StatuslineEntry — one parsed JSONL line from a Claude Code project log.
// Reference: ADR 0005 jsonl-data-source.md
// parsed_ok=false means the line failed JSON parse or was missing required fields;
// CDataManager::Tick() skips such entries.
//
// On JSONL data source (not sidecar), session_id is derived from the JSONL
// file's UUID filename, and cwd is read from the first user message in the
// file (populated lazily on first parse).
struct StatuslineEntry
{
    std::wstring session_id;     // derived from JSONL file UUID
    std::wstring cwd;            // read from JSONL first user message
    std::wstring model;
    unsigned long long input{};
    unsigned long long output{};
    unsigned long long cache_creation{};
    unsigned long long cache_read{};
    ULONGLONG received_ms{};      // GetTickCount64 at parse time
    bool parsed_ok{ false };
};

// CJsonlDirectoryWatcher — scans Claude Code project JSONL files
// (`~/.claude/projects/*/*.jsonl` + `~/.claude/projects/*/subagents/*.jsonl`),
// incrementally reads new lines, deduplicates by message.id (streaming
// duplicates), returns a flat list of StatuslineEntry for one Tick().
//
// Design (ADR 0005):
//   - For each watched .jsonl file, track the byte offset we last read to.
//   - Parse `type=assistant` lines; dedupe by message.id keeping the LAST value
//     (gille.ai warns that streaming intermediate lines have placeholder 0/1
//     values; the final line of a completed turn is the true value).
//   - Emit one StatuslineEntry per *final* message.id per tick (i.e. the entry
//     for which the seen→last_seen transition happened in the most recent
//     tail window).
class CJsonlDirectoryWatcher
{
public:
    CJsonlDirectoryWatcher();
    ~CJsonlDirectoryWatcher();

    // Configure the root directories to scan. Defaults: ~/.claude/projects
    // (CLI) and %APPDATA%/Claude/local-agent-mode-sessions (Desktop, future).
    void SetRootDirs(const std::vector<std::wstring>& roots) { m_roots = roots; }
    const std::vector<std::wstring>& GetRootDirs() const { return m_roots; }

    // Read new lines appended since the previous call (or Reset).
    // Returns parsed StatuslineEntry vector across all watched files.
    // Failed parses get parsed_ok=false (caller skips).
    std::vector<StatuslineEntry> DrainNewEntries();

    // Drop all per-file offset state (called on plugin reload to skip
    // historical data, just like the old CSidecarReader::ResetOffset).
    void Reset();

    // Rescan the root dirs and add any new .jsonl files to the watch set.
    // Called once per tick (cheap) to pick up files created by new sessions.
    void RefreshWatchSet();

private:
    // For one specific file, read the new bytes since m_last_offsets[path]
    // and emit (deduplicated, msg_id-stable) StatuslineEntry records.
    // New last-seen msg_ids in the tail window are emitted exactly once.
    std::vector<StatuslineEntry> ReadOneFile(const std::wstring& path);

    // Open file in shared-read mode, return handle or INVALID_HANDLE_VALUE.
    HANDLE OpenForRead(const std::wstring& path);

    // Per-file byte offset of next byte to read.
    std::map<std::wstring, uint64_t> m_last_offsets;

    // Per-file dedupe state: msg_id -> (input, output, cache_creation, cache_read)
    // We only EMIT an entry for a msg_id when its state changes (i.e. a
    // streaming update overwrote the prior values). Once emitted, we record
    // it in m_emitted_ids to avoid emitting it again on subsequent ticks.
    //
    // Note: key type is std::wstring; we widen msg_id from UTF-8 std::string
    // (from nlohmann::json) to wstring at the entry boundary.
    struct FinalUsage {
        unsigned long long input{};
        unsigned long long output{};
        unsigned long long cache_creation{};
        unsigned long long cache_read{};
    };
    std::map<std::wstring, std::map<std::wstring, FinalUsage>> m_seen;   // path -> msg_id -> usage
    std::map<std::wstring, std::set<std::wstring>> m_emitted;             // path -> set of already-emitted msg_ids

    // Set of files currently being watched (subset of m_roots enumeration).
    std::set<std::wstring> m_watched_files;

    // Root directories to scan.
    std::vector<std::wstring> m_roots;
};
