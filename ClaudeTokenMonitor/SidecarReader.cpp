// SidecarReader.cpp — CSidecarReader::DrainNewEntries skeleton.
// Reference: plan §4.2 step 1; 03-data-flow.md §跳 5
// TODO detail: .claude/skills/claude-token-monitor/references/topics/data-source.md

#include "pch.h"
#include "SidecarReader.h"
#include "JsonHelpers.h"
#include <fstream>
#include <sstream>

CSidecarReader::CSidecarReader()
{
    // TODO: m_path = CStatuslineInstaller::GetSidecarPath(); m_hFile = INVALID_HANDLE_VALUE;
    // See: StatuslineInstaller.h::GetSidecarPath (default = %APPDATA%\ClaudeTokenMonitor\sidecar.jsonl)
}

CSidecarReader::~CSidecarReader()
{
    CloseHandle();
}

void CSidecarReader::OpenHandle()
{
    // TODO: CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
    //                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    // See: references/topics/data-source.md (shared-read semantics)
}

void CSidecarReader::CloseHandle()
{
    // TODO: if m_hFile != INVALID_HANDLE_VALUE → CloseHandle(m_hFile); m_hFile = INVALID_HANDLE_VALUE;
}

bool CSidecarReader::ReadChunk(std::string& out_buf)
{
    out_buf.clear();
    // TODO: query current EOF via GetFileSizeEx; read from m_last_offset to EOF;
    //       append to out_buf; update m_last_offset.
    // See: references/topics/data-source.md (offset tracking)
    return false;
}

void CSidecarReader::ResetOffset()
{
    // TODO: query current EOF size and set m_last_offset = that value;
    //       close+reopen handle to drop any cached read pointer.
    // Used by CClaudeTokenMonitorPlugin::OnInitialize to skip historical sidecar lines
    // after FreeLibrary+LoadLibrary reload. See plan §7 risk table.
}

std::vector<StatuslineEntry> CSidecarReader::DrainNewEntries()
{
    std::vector<StatuslineEntry> entries;
    // TODO:
    //   1. If m_hFile == INVALID_HANDLE_VALUE → OpenHandle(); return empty on failure.
    //   2. std::string buf; if (!ReadChunk(buf)) return empty;
    //   3. Split buf by '\n' (preserve partial last line for next call).
    //   4. For each complete line: ULONGLONG now = GetTickCount64();
    //      entries.push_back(JsonHelpers::ParseStatuslineJson(line, now));
    //   5. Return entries (caller filters parsed_ok==false).
    // See: plan §4.2 step 1; references/topics/data-source.md
    return entries;
}