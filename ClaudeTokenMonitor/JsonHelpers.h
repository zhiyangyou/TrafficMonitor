#pragma once
// JsonHelpers.h — DEPRECATED. Originally parsed statusline wrapper JSON.
// As of ADR 0005 (2026-06-14) we read JSONL directly; parsing is done in
// SidecarReader.cpp's anonymous namespace. This header is kept as a stub to
// avoid breaking any straggler #include; new code should not use it.

#include "pch.h"
#include <string>

namespace JsonHelpers
{
    // Stub kept for ABI compatibility. Returns parsed_ok=false always.
    // CJsonlDirectoryWatcher::ReadOneFile does the real work inline.
    struct StatuslineEntry;  // forward decl to satisfy legacy includers
}
