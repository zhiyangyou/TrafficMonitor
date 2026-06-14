// SidecarReader.cpp — CSidecarReader shared-read + incremental line drain.
// Reference: plan section 4.2 step 1; references/topics/data-source.md
//
// Behavior:
//   - OpenHandle: CreateFileW with FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE
//     so the PowerShell wrapper (which opens the file in Append mode) can keep writing
//     while we read. OPEN_ALWAYS creates the file if missing (returns a 0-byte handle).
//   - ReadChunk: query EOF via GetFileSizeEx; read from m_last_offset to EOF.
//     If EOF < m_last_offset, treat as truncation (file was rotated/deleted) and
//     reset to 0 so we re-read from the start of whatever is now on disk.
//   - DrainNewEntries: concat m_residual_buffer with new chunk, split on '\n',
//     strip trailing '\r' (Windows line endings), parse each complete line.
//     The trailing partial line (no terminating '\n') stays in m_residual_buffer
//     for the next call. GetTickCount64() is stamped into received_ms for the
//     fresh-sliding-window delta in CPerSessionAccumulator.

#include "pch.h"
#include "SidecarReader.h"
#include "StatuslineInstaller.h"
#include "JsonHelpers.h"
#include <string>

CSidecarReader::CSidecarReader()
{
    // m_hFile is default-initialized to INVALID_HANDLE_VALUE in the header.
    m_last_offset = 0;
    m_path = CStatuslineInstaller::GetSidecarPath();
    m_residual_buffer.clear();
}

CSidecarReader::~CSidecarReader()
{
    CloseHandle();
}

void CSidecarReader::OpenHandle()
{
    if (m_hFile != INVALID_HANDLE_VALUE)
    {
        return; // already open
    }
    if (m_path.empty())
    {
        // Installer not yet functional — nothing to open.
        return;
    }

    m_hFile = CreateFileW(
        m_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,          // create if missing, open if present
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (m_hFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    // First open: seek to file start so the very first DrainNewEntries()
    // picks up any pre-existing content. Subsequent calls preserve m_last_offset
    // (ReadChunk seeks before each read).
    if (m_last_offset == 0)
    {
        LARGE_INTEGER zero{};
        zero.QuadPart = 0;
        SetFilePointerEx(m_hFile, zero, nullptr, FILE_BEGIN);
    }
}

void CSidecarReader::CloseHandle()
{
    if (m_hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
}

bool CSidecarReader::ReadChunk(std::string& out_buf)
{
    out_buf.clear();

    if (m_hFile == INVALID_HANDLE_VALUE)
    {
        OpenHandle();
        if (m_hFile == INVALID_HANDLE_VALUE)
        {
            return false;
        }
    }

    // Query current EOF.
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(m_hFile, &fileSize))
    {
        return false;
    }
    uint64_t eof = static_cast<uint64_t>(fileSize.QuadPart);

    // File was truncated/rotated/delted between calls. Reset to 0 and
    // re-read from start of whatever is now on disk.
    if (m_last_offset > eof)
    {
        m_last_offset = 0;
        m_residual_buffer.clear();
        LARGE_INTEGER zero{};
        zero.QuadPart = 0;
        SetFilePointerEx(m_hFile, zero, nullptr, FILE_BEGIN);
    }

    // Nothing new to read.
    if (m_last_offset >= eof)
    {
        return true;
    }

    // Seek to where we left off.
    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(m_last_offset);
    if (!SetFilePointerEx(m_hFile, offset, nullptr, FILE_BEGIN))
    {
        return false;
    }

    // Drain everything from m_last_offset to EOF. Loop because a single
    // ReadFile is not guaranteed to fill the buffer in one call.
    char buffer[8192];
    for (;;)
    {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(m_hFile, buffer, sizeof(buffer), &bytesRead, nullptr);
        if (!ok || bytesRead == 0)
        {
            break;
        }
        out_buf.append(buffer, bytesRead);
        m_last_offset += bytesRead;
    }

    return true;
}

void CSidecarReader::ResetOffset()
{
    // Drop the old handle and read state, then reopen and seek to the current
    // EOF so subsequent calls pick up only new writes. Used by
    // CClaudeTokenMonitorPlugin::OnInitialize to skip historical sidecar
    // content after a FreeLibrary+LoadLibrary cycle.
    m_last_offset = 0;
    m_residual_buffer.clear();
    CloseHandle();

    if (m_path.empty())
    {
        m_path = CStatuslineInstaller::GetSidecarPath();
    }
    if (!m_path.empty())
    {
        OpenHandle();
        if (m_hFile != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER fileSize{};
            if (GetFileSizeEx(m_hFile, &fileSize))
            {
                m_last_offset = static_cast<uint64_t>(fileSize.QuadPart);
            }
        }
    }
}

std::vector<StatuslineEntry> CSidecarReader::DrainNewEntries()
{
    std::vector<StatuslineEntry> entries;

    std::string chunk;
    if (!ReadChunk(chunk))
    {
        return entries; // empty: caller will treat as "no new data"
    }

    if (chunk.empty() && m_residual_buffer.empty())
    {
        return entries;
    }

    // Prepend any partial line carried over from a previous call.
    std::string combined;
    combined.reserve(m_residual_buffer.size() + chunk.size());
    combined.append(m_residual_buffer);
    combined.append(chunk);
    m_residual_buffer.clear();

    // Split on '\n'. Each complete line is parsed immediately; the trailing
    // fragment (if any) is saved for the next call.
    const ULONGLONG now_ms = GetTickCount64();
    size_t start = 0;
    for (;;)
    {
        size_t pos = combined.find('\n', start);
        if (pos == std::string::npos)
        {
            break;
        }
        std::string line = combined.substr(start, pos - start);
        // Strip trailing '\r' (PowerShell WriteLine on Windows may emit CRLF).
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            entries.push_back(JsonHelpers::ParseStatuslineJson(line, now_ms));
        }
        start = pos + 1;
    }

    // Save trailing partial line.
    if (start < combined.size())
    {
        m_residual_buffer = combined.substr(start);
    }

    return entries;
}
