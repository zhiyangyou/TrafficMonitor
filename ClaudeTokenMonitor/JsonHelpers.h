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

        try
        {
            auto j = nlohmann::json::parse(line);

            // session_id — required (non-empty string). Missing/empty => skip.
            if (j.contains("session_id") && j["session_id"].is_string())
            {
                std::string sid = j["session_id"].get<std::string>();
                e.session_id = std::wstring(sid.begin(), sid.end());
            }
            if (e.session_id.empty())
            {
                // No session_id: cannot aggregate; mark unparsed and return.
                return e;
            }

            // cwd — optional but kept on entry for future use / debug.
            if (j.contains("cwd") && j["cwd"].is_string())
            {
                std::string cwd = j["cwd"].get<std::string>();
                e.cwd = std::wstring(cwd.begin(), cwd.end());
            }

            // model.display_name — optional.
            if (j.contains("model") && j["model"].is_object())
            {
                auto& m = j["model"];
                if (m.contains("display_name") && m["display_name"].is_string())
                {
                    std::string mn = m["display_name"].get<std::string>();
                    e.model = std::wstring(mn.begin(), mn.end());
                }
            }

            // context_window.current_usage.{input,output,cache_creation,cache_read}
            // current_usage may be null (first API call / after /compact) — skip entire entry.
            if (j.contains("context_window") && j["context_window"].is_object())
            {
                auto& cw = j["context_window"];
                if (cw.contains("current_usage")
                    && cw["current_usage"].is_object()
                    && !cw["current_usage"].is_null())
                {
                    auto& cu = cw["current_usage"];
                    e.input           = cu.value("input_tokens", 0ULL);
                    e.output          = cu.value("output_tokens", 0ULL);
                    e.cache_creation  = cu.value("cache_creation_input_tokens", 0ULL);
                    e.cache_read      = cu.value("cache_read_input_tokens", 0ULL);
                }
                else
                {
                    // current_usage is null or missing — cannot compute delta; skip.
                    return e;
                }
            }
            else
            {
                // No context_window block at all — skip.
                return e;
            }

            // wrapper_ms is read by the sidecar writer (PowerShell wrapper) but not
            // required here; freshness checks happen in CDataManager::Tick().
            e.parsed_ok = true;
        }
        catch (const std::exception&)
        {
            // Malformed JSON, partial line, or type mismatch — keep parsed_ok=false.
        }

        return e;
    }
}