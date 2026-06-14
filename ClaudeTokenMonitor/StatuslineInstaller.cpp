// StatuslineInstaller.cpp — wrapper install/uninstall + path helpers + embedded PowerShell.
// Reference: plan section 4.4 / 4.5 / 5; references/topics/wrapper-installer.md

#include "pch.h"
#include "StatuslineInstaller.h"
#include <ShlObj.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <nlohmann/json.hpp>

// Embedded PowerShell wrapper script — written to %APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1
// by Install(). Full skeleton per plan section 5 (statusline-wrapper.ps1).
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

namespace
{
    // Return current Unix epoch time as a decimal string (e.g. "1718371200").
    std::wstring GetUnixTimestampStr()
    {
        std::time_t t = std::time(nullptr);
        wchar_t buf[32];
        swprintf_s(buf, _countof(buf), L"%lld", static_cast<long long>(t));
        return std::wstring(buf);
    }

    // Read an entire file into a string. Returns false on failure.
    bool ReadFileToString(const std::wstring& path, std::string& out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    // Strip an optional UTF-8 BOM from a string in-place.
    void StripUtf8Bom(std::string& s)
    {
        if (s.size() >= 3 &&
            static_cast<unsigned char>(s[0]) == 0xEF &&
            static_cast<unsigned char>(s[1]) == 0xBB &&
            static_cast<unsigned char>(s[2]) == 0xBF)
        {
            s.erase(0, 3);
        }
    }

    // Atomic write: write content to <path>.tmp (with UTF-8 BOM) then rename to <path>.
    // Returns true on success. Does not throw (MFC DLL constraint).
    bool AtomicWriteUtf8(const std::wstring& path, const std::string& content)
    {
        std::wstring tmp = path;
        tmp += L".tmp";

        // Write to tmp file (binary to avoid CRLF translation).
        {
            std::ofstream of(tmp, std::ios::binary | std::ios::trunc);
            if (!of)
            {
                return false;
            }
            // UTF-8 BOM
            of.write("\xEF\xBB\xBF", 3);
            of.write(content.data(), static_cast<std::streamsize>(content.size()));
            of.flush();
            if (!of.good())
            {
                of.close();
                DeleteFileW(tmp.c_str());
                return false;
            }
        }

        // Replace existing file atomically.
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tmp.c_str());
            return false;
        }
        return true;
    }

    // Convert a UTF-8 std::string to std::wstring (best-effort, lossy on invalid sequences).
    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
        {
            return std::wstring();
        }
        int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        if (needed <= 0)
        {
            return std::wstring();
        }
        std::wstring out(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], needed);
        return out;
    }

    // Convert a std::wstring to a UTF-8 std::string.
    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty())
        {
            return std::string();
        }
        int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return std::string();
        }
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], needed, nullptr, nullptr);
        return out;
    }

    // Copy a file (binary). Returns true on success.
    bool CopyFileBinary(const std::wstring& src, const std::wstring& dst)
    {
        return CopyFileW(src.c_str(), dst.c_str(), FALSE) != 0;
    }
}

std::wstring CStatuslineInstaller::GetAppDataDir()
{
    wchar_t* pPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &pPath);
    if (FAILED(hr) || pPath == nullptr)
    {
        if (pPath) CoTaskMemFree(pPath);
        return L"";
    }
    std::wstring dir = pPath;
    CoTaskMemFree(pPath);
    dir += L"\\ClaudeTokenMonitor";
    // Ensure directory exists; ignore ERROR_ALREADY_EXISTS.
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring CStatuslineInstaller::GetWrapperPath()
{
    return GetAppDataDir() + L"\\statusline-wrapper.ps1";
}

std::wstring CStatuslineInstaller::GetSidecarPath()
{
    return GetAppDataDir() + L"\\sidecar.jsonl";
}

std::wstring CStatuslineInstaller::GetBackupPath()
{
    return GetAppDataDir() + L"\\settings.json.bak." + GetUnixTimestampStr();
}

std::wstring CStatuslineInstaller::GetPreviousStatuslinePath()
{
    return GetAppDataDir() + L"\\previous-statusline.txt";
}

std::wstring CStatuslineInstaller::GetClaudeSettingsPath()
{
    wchar_t profile[MAX_PATH] = { 0 };
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return L"";
    }
    return std::wstring(profile) + L"\\.claude\\settings.json";
}

CStatuslineInstaller::InstallState CStatuslineInstaller::CheckInstalled()
{
    std::wstring settings_path = GetClaudeSettingsPath();
    if (settings_path.empty())
    {
        return InstallState::ClaudeCodeMissing;
    }

    // Check file existence via attributes (cheaper than ifstream).
    DWORD attrs = GetFileAttributesW(settings_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        return InstallState::ClaudeCodeMissing;
    }

    // Coarse detection: substring search for our wrapper filename in raw file content.
    // Avoids nlohmann/json dependency for the check path (settings.json may be malformed
    // or have null current_usage; we only need to know whether we installed).
    std::string content;
    if (!ReadFileToString(settings_path, content))
    {
        return InstallState::NotInstalled;
    }

    if (content.find("statusline-wrapper.ps1") != std::string::npos)
    {
        return InstallState::Installed;
    }
    return InstallState::NotInstalled;
}

bool CStatuslineInstaller::Install()
{
    // 1. Precondition: Claude Code must be present.
    std::wstring settings_path = GetClaudeSettingsPath();
    if (settings_path.empty())
    {
        return false;
    }
    DWORD attrs = GetFileAttributesW(settings_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }

    // 2. Ensure %APPDATA%\ClaudeTokenMonitor exists.
    std::wstring appdata = GetAppDataDir();
    if (appdata.empty())
    {
        return false;
    }

    // 3. Read current settings.json.
    std::string raw;
    if (!ReadFileToString(settings_path, raw))
    {
        return false;
    }
    StripUtf8Bom(raw);

    // 4. Backup current settings.json (overwrite prior .bak). Use a stable
    // timestamp so the filename is deterministic — multiple installs keep only
    // the latest backup, per plan section 4.4 step 2.
    {
        std::wstring backup = GetAppDataDir() + L"\\settings.json.bak";
        CopyFileBinary(settings_path, backup);
    }

    // 5. Parse JSON and extract the previous statusLine.command (if any).
    std::string previous_cmd;
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(raw);
    }
    catch (const std::exception&)
    {
        // Malformed settings.json — start from a fresh object.
        j = nlohmann::json::object();
    }

    if (j.contains("statusLine") && j["statusLine"].is_object())
    {
        const auto& sl = j["statusLine"];
        if (sl.contains("command") && sl["command"].is_string())
        {
            previous_cmd = sl["command"].get<std::string>();
        }
    }

    // 6. Persist previous command (only if non-empty).
    {
        // If there was a previous command AND it was our own wrapper (re-install
        // case), prefer the saved originalCommand to chain back to the real original.
        std::string orig_from_settings;
        if (j.contains("statusLine") && j["statusLine"].is_object() &&
            j["statusLine"].contains("originalCommand") && j["statusLine"]["originalCommand"].is_string())
        {
            orig_from_settings = j["statusLine"]["originalCommand"].get<std::string>();
        }
        if (!orig_from_settings.empty())
        {
            previous_cmd = orig_from_settings;
        }

        if (!previous_cmd.empty())
        {
            std::ofstream prev(GetPreviousStatuslinePath(), std::ios::binary | std::ios::trunc);
            if (prev)
            {
                prev << previous_cmd;
                prev.close();
            }
            else
            {
                return false;
            }
        }
        else
        {
            // No prior command — make sure the leftover file is not present.
            DeleteFileW(GetPreviousStatuslinePath().c_str());
        }
    }

    // 7. Build the new statusLine object: command = our wrapper, originalCommand = previous.
    {
        nlohmann::json status_line = nlohmann::json::object();
        std::wstring wrapper_path = GetWrapperPath();
        std::wstring command = L"powershell -ExecutionPolicy Bypass -File \"" + wrapper_path + L"\"";
        status_line["command"] = WideToUtf8(command);
        if (!previous_cmd.empty())
        {
            status_line["originalCommand"] = previous_cmd;
        }
        j["statusLine"] = status_line;
    }

    // 8. Atomic write back to settings.json.
    std::string serialized = j.dump(4);
    serialized.push_back('\n');
    if (!AtomicWriteUtf8(settings_path, serialized))
    {
        return false;
    }

    // 9. Write wrapper script to %APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1.
    {
        std::wstring wrapper_path = GetWrapperPath();
        std::ofstream wf(wrapper_path, std::ios::binary | std::ios::trunc);
        if (!wf)
        {
            return false;
        }
        // UTF-8 BOM.
        wf.write("\xEF\xBB\xBF", 3);
        // Convert wide wrapper content to UTF-8 and write.
        std::wstring wrapper_wide(kWrapperScriptContent);
        std::string wrapper_utf8 = WideToUtf8(wrapper_wide);
        wf.write(wrapper_utf8.data(), static_cast<std::streamsize>(wrapper_utf8.size()));
        wf.flush();
        if (!wf.good())
        {
            wf.close();
            return false;
        }
    }

    // 10. Create empty sidecar.jsonl if missing.
    {
        std::wstring sidecar = GetSidecarPath();
        DWORD sa = GetFileAttributesW(sidecar.c_str());
        if (sa == INVALID_FILE_ATTRIBUTES)
        {
            std::ofstream sf(sidecar, std::ios::binary | std::ios::app);
            sf.close();
        }
    }

    return true;
}

bool CStatuslineInstaller::Uninstall()
{
    // 1. If settings.json missing → nothing to undo.
    std::wstring settings_path = GetClaudeSettingsPath();
    if (settings_path.empty())
    {
        return true;
    }
    DWORD attrs = GetFileAttributesW(settings_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        return true;
    }

    // 2. Read + parse JSON.
    std::string raw;
    if (!ReadFileToString(settings_path, raw))
    {
        return true;  // Can't read → noop.
    }
    StripUtf8Bom(raw);

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(raw);
    }
    catch (const std::exception&)
    {
        return true;  // Malformed → noop.
    }

    // 3. If statusLine.command does not reference our wrapper → noop.
    if (!j.contains("statusLine") || !j["statusLine"].is_object())
    {
        return true;
    }
    auto& sl = j["statusLine"];
    if (!sl.contains("command") || !sl["command"].is_string())
    {
        return true;
    }
    std::string cmd = sl["command"].get<std::string>();
    if (cmd.find("statusline-wrapper.ps1") == std::string::npos)
    {
        return true;
    }

    // 4. Restore from previous-statusline.txt, or remove statusLine key.
    std::string previous_cmd;
    {
        std::ifstream prev(GetPreviousStatuslinePath(), std::ios::binary);
        if (prev)
        {
            std::ostringstream ss;
            ss << prev.rdbuf();
            previous_cmd = ss.str();
            // Trim trailing CR/LF/whitespace.
            while (!previous_cmd.empty() &&
                   (previous_cmd.back() == '\n' || previous_cmd.back() == '\r' ||
                    previous_cmd.back() == ' ' || previous_cmd.back() == '\t'))
            {
                previous_cmd.pop_back();
            }
        }
    }

    if (!previous_cmd.empty())
    {
        // Restore: command = previous, drop originalCommand.
        sl["command"] = previous_cmd;
        if (sl.contains("originalCommand"))
        {
            sl.erase("originalCommand");
        }
    }
    else
    {
        // No previous → remove entire statusLine key.
        j.erase("statusLine");
    }

    // 5. Atomic write back.
    std::string serialized = j.dump(4);
    serialized.push_back('\n');
    if (!AtomicWriteUtf8(settings_path, serialized))
    {
        return false;
    }

    // 6. Clean up previous-statusline.txt (per plan section 4.5 — wrapper script and
    // sidecar are intentionally preserved for re-install).
    DeleteFileW(GetPreviousStatuslinePath().c_str());

    return true;
}
