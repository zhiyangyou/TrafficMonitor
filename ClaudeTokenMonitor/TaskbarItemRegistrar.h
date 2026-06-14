// TaskbarItemRegistrar.h — write 4 plugin item IDs into main program's config.ini
// so the items appear on the taskbar without manual user check-in Options.
// See plan §5 "plugin_display_item ini 兜底".

#pragma once
#include "pch.h"
#include <string>
#include <vector>

class ITrafficMonitor;

class CTaskbarItemRegistrar {
public:
    // 4 item ids in fixed order: Input / CacheWrite / CacheRead / Output.
    static const std::vector<std::wstring>& GetItemIds();

    // Read ini at <config_dir>\config.ini, ensure all 4 ids are present in
    // [task_bar] plugin_display_item, rewrite the file if anything changed.
    // config_dir: from ITrafficMonitor::GetPluginConfigDir()
    // pApp: optional, used to show a restart notification on success.
    // Returns true only when the file was actually updated.
    static bool EnsureRegistered(const std::wstring& config_dir,
                                  ITrafficMonitor* pApp = nullptr);

    // Throttled re-patch: re-runs EnsureRegistered at most once every
    // `min_interval_ms` milliseconds. Used to recover from the main
    // program's SaveConfig() clobbering our ini writes after first load.
    // No-op if last patch was < min_interval_ms ago.
    // Throws nothing; safe to call from DataRequired (1Hz).
    static void EnsureRegisteredThrottled(const std::wstring& config_dir,
                                          ITrafficMonitor* pApp,
                                          ULONGLONG min_interval_ms = 5000);

    // Reset throttling state (e.g. on plugin reload).
    static void ResetThrottle();

private:
    static std::wstring GetConfigIniPath(const std::wstring& config_dir);
    static std::vector<std::wstring> ParsePluginDisplayItem(const std::string& ini_content);
    static std::string SerializePluginDisplayItem(const std::vector<std::wstring>& ids);
    static bool AtomicWriteUtf8(const std::wstring& path, const std::string& content);
};
