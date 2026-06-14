#pragma once
#include <string>
#include <vector>
#include <windows.h>

// StatuslineEntry — one parsed JSONL line from the sidecar.
// Reference: plan §3 SidecarReader.h; 03-data-flow.md §跳 5
// parsed_ok=false means the line failed JSON parse or was missing required fields;
// CDataManager::Tick() skips such entries.
struct StatuslineEntry
{
    std::wstring session_id;
    std::wstring cwd;
    std::wstring model;
    unsigned long long input{};
    unsigned long long output{};
    unsigned long long cache_creation{};
    unsigned long long cache_read{};
    ULONGLONG received_ms{};
    bool parsed_ok{ false };
};

// CSidecarReader — opens the sidecar JSONL in shared-read mode and incrementally
// reads new lines since the last DrainNewEntries() call.
// Reference: plan §4.2 step 1; 03-data-flow.md §跳 5
// TODO detail: .claude/skills/claude-token-monitor/references/topics/data-source.md
class CSidecarReader
{
public:
    CSidecarReader();
    ~CSidecarReader();

    // Read new lines appended since the previous call (or ResetOffset).
    // Returns parsed StatuslineEntry vector in file-order. Failed parses
    // are included with parsed_ok=false so the caller can decide to skip.
    std::vector<StatuslineEntry> DrainNewEntries();

    // Reset file offset to current EOF (called from CDataManager::Tick()
    // when first loaded, to avoid ingesting historical sidecar lines).
    void ResetOffset();

    // Override sidecar path (default = %APPDATA%\ClaudeTokenMonitor\sidecar.jsonl).
    void SetPath(const std::wstring& path) { m_path = path; }
    const std::wstring& GetPath() const { return m_path; }

private:
    // TODO: open file via CreateFileW(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, ...)
    // TODO: track m_last_offset, read from offset to EOF, split by '\n', call JsonHelpers::ParseStatuslineJson
    // See: plan §4.2 step 1; references/topics/data-source.md
    void OpenHandle();
    void CloseHandle();
    bool ReadChunk(std::string& out_buf);

    std::wstring m_path;
    HANDLE m_hFile{ INVALID_HANDLE_VALUE };
    ULONGLONG m_last_offset{};
    // Holds partial trailing bytes from previous ReadChunk that did not end on '\n'.
    // Concatenated with the next chunk before line splitting. Cleared once a
    // complete line is recovered.
    std::string m_residual_buffer;
};