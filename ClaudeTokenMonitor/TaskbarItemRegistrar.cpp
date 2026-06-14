// TaskbarItemRegistrar.cpp — see TaskbarItemRegistrar.h for the design rationale.
// Encoding: main program writes config.ini as UTF-8 with BOM. We must match that.

#include "pch.h"
#include "TaskbarItemRegistrar.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <PluginInterface.h>  // ITrafficMonitor

const std::vector<std::wstring>& CTaskbarItemRegistrar::GetItemIds() {
    // Order must match the 4 CTokenItem IDs declared in CClaudeTokenMonitorPlugin.
    static const std::vector<std::wstring> ids = {
        L"CTM_TokenIn_v1",
        L"CTM_TokenCacheWrite_v1",
        L"CTM_TokenCacheRead_v1",
        L"CTM_TokenOut_v1"
    };
    return ids;
}

std::wstring CTaskbarItemRegistrar::GetConfigIniPath(const std::wstring& config_dir) {
    if (config_dir.empty()) return L"";
    return config_dir + L"\\config.ini";
}

std::vector<std::wstring> CTaskbarItemRegistrar::ParsePluginDisplayItem(const std::string& ini_content) {
    // Find [task_bar] section header.
    std::vector<std::wstring> result;
    size_t sectionPos = ini_content.find("[task_bar]");
    if (sectionPos == std::string::npos) return result;

    // Section ends at the next newline (start of next section / EOF).
    size_t sectionEnd = ini_content.find('\n', sectionPos);
    if (sectionEnd == std::string::npos) sectionEnd = ini_content.size();

    // Look for the key within the [task_bar] section only.
    size_t keyPos = ini_content.find("plugin_display_item", sectionPos);
    if (keyPos == std::string::npos || keyPos > sectionEnd) return result;

    // Value spans from after '=' to the line end.
    size_t eq = ini_content.find('=', keyPos);
    if (eq == std::string::npos) return result;
    size_t valStart = eq + 1;
    size_t valEnd = ini_content.find('\n', valStart);
    if (valEnd == std::string::npos) valEnd = ini_content.size();

    std::string val = ini_content.substr(valStart, valEnd - valStart);
    // Strip trailing CR and surrounding whitespace.
    while (!val.empty() && (val.back() == '\r' || val.back() == ' ' || val.back() == '\t')) val.pop_back();
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);

    if (val.empty()) return result;

    // Split on ',' and trim each entry.
    std::stringstream ss(val);
    std::string token;
    while (std::getline(ss, token, ',')) {
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.pop_back();
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.erase(0, 1);
        if (!token.empty()) {
            std::wstring w(token.begin(), token.end());
            result.push_back(w);
        }
    }
    return result;
}

std::string CTaskbarItemRegistrar::SerializePluginDisplayItem(const std::vector<std::wstring>& ids) {
    std::string out;
    for (size_t i = 0; i < ids.size(); i++) {
        if (i > 0) out += ',';
        out += std::string(ids[i].begin(), ids[i].end());
    }
    return out;
}

bool CTaskbarItemRegistrar::AtomicWriteUtf8(const std::wstring& path, const std::string& content) {
    // Write to a sibling temp file first, then atomically rename.
    // This avoids leaving a half-written file if the main program is reading it
    // concurrently (e.g. when the user clicks "OK" in Options).
    std::wstring tmp = path + L".tmp";
    std::ofstream of(tmp, std::ios::binary);
    if (!of) return false;
    of << "\xEF\xBB\xBF";  // UTF-8 BOM — required by the main program's IniHelper.
    of << content;
    of.close();
    if (of.fail()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool CTaskbarItemRegistrar::EnsureRegistered(const std::wstring& config_dir, ITrafficMonitor* pApp) {
    std::wstring ini_path = GetConfigIniPath(config_dir);
    if (ini_path.empty()) return false;

    // Read existing ini content. If the main program hasn't created it yet
    // (e.g. first install before TrafficMonitor ever ran), bail out — there is
    // nothing to merge into.
    std::ifstream in(ini_path, std::ios::binary);
    if (!in.good()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    in.close();

    // Parse current plugin_display_item entries.
    std::vector<std::wstring> existing = ParsePluginDisplayItem(content);

    // Append any of our 4 ids that are missing. Idempotent: if all present, do nothing.
    bool changed = false;
    for (const auto& id : GetItemIds()) {
        if (std::find(existing.begin(), existing.end(), id) == existing.end()) {
            existing.push_back(id);
            changed = true;
        }
    }
    if (!changed) return false;

    std::string newValue = SerializePluginDisplayItem(existing);

    // Splice the new value back into the ini, scoped to the [task_bar] section
    // so unrelated sections are untouched.
    size_t sectionPos = content.find("[task_bar]");
    if (sectionPos == std::string::npos) {
        // Section absent — append a new [task_bar] block at the end of file.
        if (!content.empty() && content.back() != '\n') content += "\r\n";
        content += "[task_bar]\r\nplugin_display_item=" + newValue + "\r\n";
    } else {
        size_t nextSection = content.find('\n', sectionPos);
        if (nextSection == std::string::npos) nextSection = content.size();

        size_t keyPos = content.find("plugin_display_item", sectionPos);
        if (keyPos != std::string::npos && keyPos < nextSection) {
            // Key present in this section — replace its value in place.
            size_t eq = content.find('=', keyPos);
            size_t valEnd = content.find('\n', eq);
            if (valEnd == std::string::npos) valEnd = content.size();
            content.replace(eq + 1, valEnd - eq - 1, newValue);
        } else {
            // Section exists but has no plugin_display_item key — insert one
            // right after the section header.
            size_t insertPos = sectionPos + std::string("[task_bar]").size();
            // Skip past \r\n after the header.
            while (insertPos < content.size() && content[insertPos] == '\r') insertPos++;
            if (insertPos < content.size() && content[insertPos] == '\n') insertPos++;
            content.insert(insertPos, "plugin_display_item=" + newValue + "\r\n");
        }
    }

    bool ok = AtomicWriteUtf8(ini_path, content);

    // Notify the user. The main program only loads config.ini at startup, so
    // a restart is required to actually see the new items on the taskbar.
    if (ok && pApp) {
        pApp->ShowNotifyMessage(
            L"ClaudeTokenMonitor: 4 taskbar items registered. Restart TrafficMonitor to apply.");
    }
    return ok;
}

namespace {
    // Throttling state for EnsureRegisteredThrottled.
    // Updated under no synchronization: only the main thread calls DataRequired
    // (TrafficMonitor/TrafficMonitorDlg.cpp:1504), so cross-thread access is not
    // a concern. If a future version moves DataRequired off the UI thread,
    // wrap these in std::atomic<ULONGLONG>.
    ULONGLONG g_last_run_ms = 0;
    ULONGLONG g_last_seen_ini_mtime = 0;  // FILETIME-ish (Win32 FILETIME raw value)
    std::wstring g_cached_config_dir;

    // Get last-write FILETIME of a file as a single ULONGLONG for cheap comparison.
    // Returns 0 if the file doesn't exist or attributes can't be queried.
    ULONGLONG GetLastWriteTimeU64(const std::wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            return 0;
        }
        ULARGE_INTEGER li;
        li.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        li.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        return li.QuadPart;
    }
}

void CTaskbarItemRegistrar::EnsureRegisteredThrottled(const std::wstring& config_dir,
                                                      ITrafficMonitor* pApp,
                                                      ULONGLONG min_interval_ms)
{
    std::wstring ini_path = GetConfigIniPath(config_dir);
    if (ini_path.empty()) return;

    ULONGLONG now_ms = GetTickCount64();
    ULONGLONG current_mtime = GetLastWriteTimeU64(ini_path);

    // Fast skip conditions:
    // 1. We patched very recently (< min_interval_ms ago) AND
    //    nobody else touched the file since we last saw it. The "since we last
    //    saw it" check is the key: if the main program's SaveConfig() rewrote
    //    the ini, mtime advanced, and we MUST re-patch regardless of cooldown.
    bool is_too_soon = (g_last_run_ms != 0 && (now_ms - g_last_run_ms) < min_interval_ms);
    bool mtime_changed = (current_mtime != g_last_seen_ini_mtime);
    if (is_too_soon && !mtime_changed) {
        return;
    }

    g_last_run_ms = now_ms;
    g_cached_config_dir = config_dir;
    bool updated = EnsureRegistered(config_dir, pApp);
    g_last_seen_ini_mtime = current_mtime;

    // If the main program overwrote our patch after we wrote it (very likely,
    // because the main program calls SaveConfig() periodically), the next Tick
    // (1s later) will see mtime advanced and re-patch automatically. The user
    // will only need to restart TrafficMonitor once — after the very first
    // patch sticks.
    //
    // Note: notification is shown only the FIRST time (EnsureRegistered returns
    // true only on actual file change). Re-patches are silent.
    (void)updated;
}

void CTaskbarItemRegistrar::ResetThrottle()
{
    g_last_run_ms = 0;
    g_last_seen_ini_mtime = 0;
    g_cached_config_dir.clear();
}
