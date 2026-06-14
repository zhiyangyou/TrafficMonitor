#pragma once
// JsonHelpers.h — header-only wrappers around nlohmann/json (include/json.hpp).
// Used by CSidecarReader::DrainNewEntries to parse each sidecar JSONL line.
// Reference: plan §3 SidecarReader.h; 03-data-flow.md §跳 5
// TODO detail: .claude/skills/claude-token-monitor/references/topics/data-source.md

#include "pch.h"
#include "SidecarReader.h"
#include <nlohmann/json.hpp>
#include <string>

namespace JsonHelpers
{
    // Parse one statusline JSON line into StatuslineEntry.
    // Returns parsed_ok=false on parse failure or missing required fields.
    // Required: session_id (string), cwd (string), context_window.current_usage.{input_tokens, output_tokens, cache_creation_input_tokens, cache_read_input_tokens}
    // TODO: implement per plan §5 statusline schema; references/topics/data-source.md
    inline StatuslineEntry ParseStatuslineJson(const std::string& line, ULONGLONG received_ms)
    {
        StatuslineEntry e;
        e.received_ms = received_ms;
        e.parsed_ok = false;
        // TODO: nlohmann::json::parse(line) → extract session_id, cwd, model.display_name,
        //       context_window.current_usage.{input_tokens, output_tokens, cache_creation_input_tokens, cache_read_input_tokens}
        //       Set e.parsed_ok = true on success. Handle null current_usage (first API call / after /compact).
        return e;
    }
}