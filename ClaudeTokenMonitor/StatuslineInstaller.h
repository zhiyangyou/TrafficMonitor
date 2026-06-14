#pragma once
#include <string>

// CStatuslineInstaller — manages install/uninstall of the PowerShell statusline wrapper.
// Reference: plan §4.4 / §4.5 / §5
// Wrapper path: %APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1 (DLL-embedded string)
// Backup path: %APPDATA%\ClaudeTokenMonitor\settings.json.bak.<unix-ts>
// Previous statusline command: %APPDATA%\ClaudeTokenMonitor\previous-statusline.txt
// TODO detail: .claude/skills/claude-token-monitor/references/topics/wrapper-installer.md
class CStatuslineInstaller
{
public:
    enum class InstallState
    {
        NotInstalled,        // settings.json present, no wrapper installed
        Installed,           // statusLine.command points to our wrapper
        ClaudeCodeMissing    // ~/.claude/settings.json not found
    };

    // Determine current install state by reading %USERPROFILE%\.claude\settings.json.
    static InstallState CheckInstalled();

    // Backup current settings.json, write wrapper script, modify statusLine to point at wrapper.
    // No-op (returns false) if Claude Code missing or settings.json not writable.
    // Returns true on success.
    static bool Install();

    // If settings.json statusLine.command is our wrapper, restore from previous-statusline.txt
    // (or remove statusLine key if no previous). Atomic write via tmp+rename.
    // Returns true if uninstalled (or was not installed), false on error.
    static bool Uninstall();

    // Path helpers (also used by CSidecarReader sidecar path).
    static std::wstring GetWrapperPath();
    static std::wstring GetSidecarPath();
    static std::wstring GetBackupPath();
    static std::wstring GetPreviousStatuslinePath();

    // %APPDATA%\ClaudeTokenMonitor\ — all state files live here.
    static std::wstring GetAppDataDir();

    // ~/.claude/settings.json — the Claude Code config we modify.
    static std::wstring GetClaudeSettingsPath();
};