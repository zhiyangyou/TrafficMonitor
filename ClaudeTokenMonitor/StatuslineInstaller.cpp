// StatuslineInstaller.cpp — wrapper install/uninstall + path helpers + embedded PowerShell.
// Reference: plan §4.4 / §4.5 / §5; 03-data-flow.md §跳 2-3
// TODO detail (Install/Uninstall bodies): .claude/skills/claude-token-monitor/references/topics/wrapper-installer.md

#include "pch.h"
#include "StatuslineInstaller.h"
#include <ShlObj.h>
#include <filesystem>
#include <fstream>

// Embedded PowerShell wrapper script — written to %APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1
// by Install(). Full skeleton per plan §5 (statusline-wrapper.ps1).
// Reference: plan §5 wrapper 脚本骨架; 03-data-flow.md §跳 2-3
// Use a plain wide string literal (concatenated across lines) — raw-string + L prefix
// does not compose in MSVC.
static const wchar_t* kWrapperScriptContent =
L"# statusline-wrapper.ps1 - installed by ClaudeTokenMonitor plugin\n"
L"# Reference: plan section 5; 03-data-flow.md\n"
L"$ErrorActionPreference = 'SilentlyContinue'\n"
L"$json = [Console]::In.ReadToEnd()\n"
L"if ([string]::IsNullOrWhiteSpace($json)) { exit 0 }\n"
L"\n"
L"# 1. Append to sidecar JSONL\n"
L"$dir = Join-Path $env:APPDATA 'ClaudeTokenMonitor'\n"
L"if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }\n"
L"$sidecar = Join-Path $dir 'sidecar.jsonl'\n"
L"$wrapped = ($json.TrimEnd() -replace '}\\s*$', '') + ',\"wrapper_ms\":' + [int64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) + '}'\n"
L"$stream = [System.IO.File]::Open($sidecar, 'Append', 'Write', 'Read')\n"
L"$writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::UTF8)\n"
L"$writer.WriteLine($wrapped)\n"
L"$writer.Flush()\n"
L"$writer.Close()\n"
L"$stream.Close()\n"
L"\n"
L"# 2. Forward to original statusline command (statusLine.originalCommand, set by installer)\n"
L"$settings = Join-Path $env:USERPROFILE '.claude\\settings.json'\n"
L"$orig = $null\n"
L"if (Test-Path $settings) {\n"
L"    try { $orig = (Get-Content $settings -Raw | ConvertFrom-Json).statusLine.originalCommand } catch {}\n"
L"}\n"
L"if ($orig) {\n"
L"    $tmp = [System.IO.Path]::GetTempFileName()\n"
L"    Set-Content -Path $tmp -Value $json -Encoding UTF8\n"
L"    cmd /c \"type `\"$tmp`\" | $orig\" 2>$null\n"
L"    Remove-Item $tmp -Force -ErrorAction SilentlyContinue\n"
L"}\n"
L"exit 0\n";

std::wstring CStatuslineInstaller::GetAppDataDir()
{
    // TODO: SHGetKnownFolderPath(FOLDERID_RoamingAppData, ...) → append L"\\ClaudeTokenMonitor"
    //       CreateDirectoryW if missing.
    // See: references/topics/wrapper-installer.md
    return L"";
}

std::wstring CStatuslineInstaller::GetWrapperPath()
{
    // TODO: return GetAppDataDir() + L"\\statusline-wrapper.ps1"
    return L"";
}

std::wstring CStatuslineInstaller::GetSidecarPath()
{
    // TODO: return GetAppDataDir() + L"\\sidecar.jsonl"
    return L"";
}

std::wstring CStatuslineInstaller::GetBackupPath()
{
    // TODO: return GetAppDataDir() + L"\\settings.json.bak." + GetUnixTimestampSeconds()
    // Multiple installs keep only the latest backup.
    return L"";
}

std::wstring CStatuslineInstaller::GetPreviousStatuslinePath()
{
    // TODO: return GetAppDataDir() + L"\\previous-statusline.txt"
    return L"";
}

std::wstring CStatuslineInstaller::GetClaudeSettingsPath()
{
    // TODO: return %USERPROFILE% + L"\\.claude\\settings.json"
    return L"";
}

CStatuslineInstaller::InstallState CStatuslineInstaller::CheckInstalled()
{
    // TODO:
    //   1. If GetClaudeSettingsPath() doesn't exist → return ClaudeCodeMissing.
    //   2. Read file → parse JSON → inspect statusLine.command field.
    //   3. If contains "statusline-wrapper.ps1" → return Installed.
    //   4. Else return NotInstalled.
    // See: references/topics/wrapper-installer.md
    return InstallState::NotInstalled;
}

bool CStatuslineInstaller::Install()
{
    // TODO: full install flow per plan §4.4:
    //   1. CheckInstalled() must be != ClaudeCodeMissing (else return false).
    //   2. Ensure GetAppDataDir() exists.
    //   3. Backup GetClaudeSettingsPath() → GetBackupPath() (overwrite prior .bak).
    //   4. Read settings.json → statusLine.command → save to GetPreviousStatuslinePath()
    //      (only if non-empty; else leave previous-statusline.txt absent so Uninstall drops statusLine).
    //   5. Write kWrapperScriptContent to GetWrapperPath() (UTF-8, BOM optional).
    //   6. Modify settings.json statusLine: { command: "powershell -ExecutionPolicy Bypass -File <wrapper>",
    //                                       originalCommand: <previous> }.
    //   7. Atomic write via .tmp + rename.
    //   8. Create empty GetSidecarPath() if missing.
    // See: references/topics/wrapper-installer.md
    return false;
}

bool CStatuslineInstaller::Uninstall()
{
    // TODO: per plan §4.5:
    //   1. If GetClaudeSettingsPath() missing → return true (nothing to do).
    //   2. Read+parse JSON. If statusLine.command doesn't reference our wrapper → return true (noop).
    //   3. If GetPreviousStatuslinePath() exists → restore statusLine.command from it; else remove statusLine key.
    //   4. Atomic write via .tmp + rename.
    // See: references/topics/wrapper-installer.md
    return false;
}