# Plugin System

This document is a complete map of the TrafficMonitor plugin extension point. Every claim is anchored to a file and line in the repository. No recommendations, no plans; only the current state.

The plugin system is composed of three surfaces defined in `include/PluginInterface.h`, one loader/registry class (`TrafficMonitor/PluginManager.h` + `.cpp`), and the host's call-sites scattered across `TrafficMonitor/*.cpp`. Plugins are Windows DLLs discovered in the `plugins/` directory at runtime.

---

## 1. PluginInterface.h — the three interfaces

Source: `include/PluginInterface.h` (455 lines total).

### 1.1 `IPluginItem` — a single displayable item

Declared at `include/PluginInterface.h:9-152`. One instance = one row in the taskbar (or a region in the main window / skin).

| Member | Location | Purpose |
| --- | --- | --- |
| `GetItemName() const` | `include/PluginInterface.h:16` | Display name shown in pickers / manager UI |
| `GetItemId() const` | `include/PluginInterface.h:22` | Stable unique ID, used to persist per-item display choice |
| `GetItemLableText() const` | `include/PluginInterface.h:28` | Label text (Chinese label used by host renderer) |
| `GetItemValueText() const` | `include/PluginInterface.h:36` | Current value text. Header warns it is called frequently, so the plugin should cache and refresh inside `DataRequired()` |
| `GetItemValueSampleText() const` | `include/PluginInterface.h:43` | Sample string used by the host to compute width at 96 DPI |
| `IsCustomDraw() const` | `include/PluginInterface.h:53` | If `true`, plugin owns its own rendering; the three text getters above are ignored |
| `GetItemWidth() const` | `include/PluginInterface.h:64` | Minimum width at 96 DPI; host scales by DPI |
| `DrawItem(hDC, x, y, w, h, dark_mode)` | `include/PluginInterface.h:76` | Custom paint, only invoked when `IsCustomDraw()` returns `true` |
| `GetItemWidthEx(hDC) const` | `include/PluginInterface.h:88` | DPI-aware width query, takes the DC and returns the actual pixel width the host should allocate |
| `OnMouseEvent(type, x, y, hWnd, flag)` | `include/PluginInterface.h:115` | `MouseEventType` enum at `:91-98` covers `MT_LCLICKED`, `MT_RCLICKED`, `MT_DBCLICKED`, `MT_WHEEL_UP`, `MT_WHEEL_DOWN`; `MouseEventFlag` at `:100-103` exposes `MF_TASKBAR_WND`. Returning non-zero tells the host the plugin fully consumed the event |
| `OnKeboardEvent(key, ctrl, shift, alt, hWnd, flag)` | `include/PluginInterface.h:132` | Header note: spelled `OnKeboardEvent` (typo kept for API compatibility). `KeyboardEventFlag` at `:117-120` exposes `KF_TASKBAR_WND`. Non-zero return = consumed |
| `OnItemInfo(type, para1, para2)` | `include/PluginInterface.h:139` | Reserved hook (empty `ItemInfoType` enum at `:134-137`); returns `void*` |
| `IsDrawResourceUsageGraph() const` | `include/PluginInterface.h:145` | `1` = draw the small bar in the taskbar row |
| `GetResourceUsageGraphValue() const` | `include/PluginInterface.h:151` | Float in `[0.0, 1.0]` for the resource bar |

The default `IsCustomDraw()` returns `false`, the default `GetItemWidth()` returns `0`, the default `DrawItem()` / `GetItemWidthEx()` are no-ops, `OnMouseEvent` and `OnKeboardEvent` default to returning `0` (consumed by host). Plugins pick which subset they need.

### 1.2 `ITMPlugin` — the plugin object (one per DLL)

Declared at `include/PluginInterface.h:158-324`. The DLL exports a factory `TMPluginGetInstance()` returning a pointer to an object implementing this interface (see the contract comment at `:426-431`).

| Member | Location | Purpose |
| --- | --- | --- |
| `GetAPIVersion() const` | `include/PluginInterface.h:166` | Fixed `7`. Plugins should not override |
| `GetItem(int index)` | `include/PluginInterface.h:176` | Returns the `index`-th `IPluginItem*` or `nullptr` to terminate |
| `DataRequired()` | `include/PluginInterface.h:181` | Called every tick; plugins pull/cache data here |
| `ShowOptionsDialog(hParent)` | `include/PluginInterface.h:198` | Opens an MFC dialog inside the plugin. Returns one of `OR_OPTION_CHANGED` / `OR_OPTION_UNCHANGED` / `OR_OPTION_NOT_PROVIDED` (enum at `:184-189`) |
| `GetInfo(PluginInfoIndex)` | `include/PluginInterface.h:215` | Returns name / description / author / copyright / version / URL. `PluginInfoIndex` at `:201-210`: `TMI_NAME`, `TMI_DESCRIPTION`, `TMI_AUTHOR`, `TMI_COPYRIGHT`, `TMI_VERSION`, `TMI_URL`, `TMI_MAX` |
| `MonitorInfo` struct | `include/PluginInterface.h:218-231` | Snapshot pushed by host: `up_speed`, `down_speed`, `cpu_usage`, `memory_usage`, `gpu_usage`, `hdd_usage`, `cpu_temperature`, `gpu_temperature`, `hdd_temperature`, `main_board_temperature`, `cpu_freq` |
| `OnMonitorInfo(const MonitorInfo&)` | `include/PluginInterface.h:236` | Receives the snapshot each tick. **Deprecated** per the comment on `GetMonitorValue` at `include/PluginInterface.h:365` ("（ITMPlugin::OnMonitorInfo 将被弃用）"); new plugins should pull data via `ITrafficMonitor::GetMonitorValue` instead |
| `GetTooltipInfo()` | `include/PluginInterface.h:241` | Extra text appended to the tray tooltip. Returns `L""` by default |
| `OnExtenedInfo(ExtendedInfoIndex, data)` | `include/PluginInterface.h:276` | Header note: spelled `OnExtenedInfo`. Receives configuration / color / dimension info from the host. The `ExtendedInfoIndex` enum is documented below |
| `GetPluginIcon()` | `include/PluginInterface.h:281` | Returns `HICON` (cast through `void*`) for menu decoration |
| `GetCommandCount()` | `include/PluginInterface.h:287` | Number of custom commands to surface in the right-click menu |
| `GetCommandName(index)` | `include/PluginInterface.h:294` | Localized name of the `index`-th command |
| `GetCommandIcon(index)` | `include/PluginInterface.h:301` | `HICON` for the menu entry |
| `OnPluginCommand(index, hWnd, para)` | `include/PluginInterface.h:309` | Invoked when the user picks a command in the menu |
| `IsCommandChecked(index)` | `include/PluginInterface.h:316` | `1` / `0`; checked-state of a command (toggle-style commands) |
| `OnInitialize(ITrafficMonitor* pApp)` | `include/PluginInterface.h:323` | API v7 entry point; host passes its `ITrafficMonitor*` so the plugin can pull data through the reverse interface |

#### `ExtendedInfoIndex` enum (`include/PluginInterface.h:243-268`)

Color codes and configuration flags. The host passes them through `OnExtenedInfo` as `wchar_t*` strings (usually numeric).

| Index | Value | Meaning |
| --- | --- | --- |
| `EI_LABEL_TEXT_COLOR` | `include/PluginInterface.h:245` | Label text color (RGB int as decimal string) |
| `EI_VALUE_TEXT_COLOR` | `include/PluginInterface.h:246` | Value text color |
| `EI_DRAW_TASKBAR_WND` | `include/PluginInterface.h:247` | `L"1"` when drawing into taskbar window, `L"0"` otherwise |
| `EI_NAIN_WND_NET_SPEED_SHORT_MODE` | `include/PluginInterface.h:250` | Main-window net speed short mode |
| `EI_MAIN_WND_SPERATE_WITH_SPACE` | `include/PluginInterface.h:251` | Main window: separate value and unit with space |
| `EI_MAIN_WND_UNIT_BYTE` | `include/PluginInterface.h:252` | Main window: use `B` (bytes) unit |
| `EI_MAIN_WND_UNIT_SELECT` | `include/PluginInterface.h:253` | Main window: speed unit selector (`0`=auto, `1`=KB/s, `2`=MB/s) |
| `EI_MAIN_WND_NOT_SHOW_UNIT` | `include/PluginInterface.h:254` | Main window: hide unit |
| `EI_MAIN_WND_NOT_SHOW_PERCENT` | `include/PluginInterface.h:255` | Main window: hide `%` |
| `EI_TASKBAR_WND_NET_SPEED_SHORT_MODE` | `include/PluginInterface.h:258` | Taskbar-window net speed short mode |
| `EI_TASKBAR_WND_SPERATE_WITH_SPACE` | `include/PluginInterface.h:259` | Taskbar: separate value and unit with space |
| `EI_TASKBAR_WND_VALUE_RIGHT_ALIGN` | `include/PluginInterface.h:260` | Taskbar: right-align value |
| `EI_TASKBAR_WND_NET_SPEED_WIDTH` | `include/PluginInterface.h:261` | Taskbar: net speed digit count |
| `EI_TASKBAR_WND_UNIT_BYTE` | `include/PluginInterface.h:262` | Taskbar: use `B` unit |
| `EI_TASKBAR_WND_UNIT_SELECT` | `include/PluginInterface.h:263` | Taskbar: speed unit selector |
| `EI_TASKBAR_WND_NOT_SHOW_UNIT` | `include/PluginInterface.h:264` | Taskbar: hide unit |
| `EI_TASKBAR_WND_NOT_SHOW_PERCENT` | `include/PluginInterface.h:265` | Taskbar: hide `%` |
| `EI_CONFIG_DIR` | `include/PluginInterface.h:267` | Plugin configuration directory (passed once during `LoadPlugins`) |

### 1.3 `ITrafficMonitor` — host-side reverse interface

Declared at `include/PluginInterface.h:329-423`. Plugins cast the `OnInitialize` argument to this and call into the host.

| Member | Location | Purpose |
| --- | --- | --- |
| `GetAPIVersion()` | `include/PluginInterface.h:337` | Host-side API version (currently returns `0`, see `TrafficMonitor/TrafficMonitor.cpp:1390-1393`); the header at `:331-337` instructs plugins to check it before calling |
| `GetVersion()` | `include/PluginInterface.h:343` | Returns `VERSION` macro, see `TrafficMonitor/TrafficMonitor.cpp:1395-1398` |
| `GetMonitorValue(MonitorItem)` | `include/PluginInterface.h:369` | Numeric read of one of 13 items (`MonitorItem` enum at `:346-361`: `MI_UP`, `MI_DOWN`, `MI_CPU`, `MI_MEMORY`, `MI_GPU_USAGE`, `MI_CPU_TEMP`, `MI_GPU_TEMP`, `MI_HDD_TEMP`, `MI_MAIN_BOARD_TEMP`, `MI_HDD_USAGE`, `MI_CPU_FREQ`, `MI_TODAY_UP_TRAFFIC`, `MI_TODAY_DOWN_TRAFFIC`). Implemented at `TrafficMonitor/TrafficMonitor.cpp:1400-1419` |
| `GetMonitorValueString(MonitorItem, is_main_window)` | `include/PluginInterface.h:377` | Same data formatted with current settings. Implemented at `TrafficMonitor/TrafficMonitor.cpp:1421-1452` |
| `ShowNotifyMessage(strMsg)` | `include/PluginInterface.h:383` | Surfaces a notification balloon. Implemented at `TrafficMonitor/TrafficMonitor.cpp:1454-1461` |
| `GetLanguageId() const` | `include/PluginInterface.h:389` | BCP-47 language id (`unsigned short`). Implemented at `TrafficMonitor/TrafficMonitor.cpp:1463-1466` |
| `GetPluginConfigDir() const` | `include/PluginInterface.h:395` | Returns `<config_dir>\plugins\`. Implemented at `TrafficMonitor/TrafficMonitor.cpp:1468-1474` |
| `GetDPI(DPIType) const` | `include/PluginInterface.h:408` | `DPI_MAIN_WND` / `DPI_TASKBAR` (enum at `:398-402`). Implemented at `TrafficMonitor/TrafficMonitor.cpp:1476-1492` |
| `GetThemeColor() const` | `include/PluginInterface.h:414` | `COLORREF` of the system accent. Implemented at `TrafficMonitor/TrafficMonitor.cpp:1319-1322` |
| `GetStringRes(key, section)` | `include/PluginInterface.h:422` | Localized string pulled from the host's string table. Implemented at `TrafficMonitor/TrafficMonitor.cpp:1494-1497` |

The `ITrafficMonitor` contract is implemented by `CTrafficMonitorApp` itself; its interface override block sits at `TrafficMonitor/TrafficMonitor.h:202-214`.

---

## 2. `CPluginManager` — the registry and loader

Source files: `TrafficMonitor/PluginManager.h` (70 lines), `TrafficMonitor/PluginManager.cpp` (475 lines).

### 2.1 State and data

- `enum class PluginState` at `TrafficMonitor/PluginManager.h:14-21`: `PS_SUCCEED`, `PS_MUDULE_LOAD_FAILED`, `PS_FUNCTION_GET_FAILED`, `PS_VERSION_NOT_SUPPORT`, `PS_DISABLE` (note: `MUDULE` is the spelling in source).
- `struct PluginInfo` at `TrafficMonitor/PluginManager.h:24-34`: stores `file_path`, `plugin_module` (HMODULE), `plugin` (ITMPlugin*), `plugin_items` (vector<IPluginItem*>), `state`, `error_code`, plus a `properties` map of all `PluginInfoIndex -> wstring`. `Property(index)` helper at `TrafficMonitor/PluginManager.cpp:234-240`.
- Internal members of `CPluginManager` at `TrafficMonitor/PluginManager.h:64-68`:
  - `m_plugins`: flat vector of all `IPluginItem*` across all loaded DLLs.
  - `m_modules`: vector of `PluginInfo` (one per DLL).
  - `m_all_display_items_with_plugins`: `set<CommonDisplayItem>` used by the taskbar to iterate display items.
  - `m_plguin_item_map` (note typo): `IPluginItem* -> ITMPlugin*` for reverse lookup.

### 2.2 `LoadPlugins()` flow

`TrafficMonitor/PluginManager.cpp:35-135`. Steps in order:

1. Builds `plugin_dir = CCommon::GetModuleDir() + L"plugins"` (`TrafficMonitor/PluginManager.cpp:39`).
2. Enumerates `*.dll` files via `CCommon::GetFiles` at `TrafficMonitor/PluginManager.cpp:41`.
3. For each DLL, appends a `PluginInfo{}` and computes `plugin_info.file_path` (`TrafficMonitor/PluginManager.cpp:45-48`).
4. Strips leading `\\` or `/` from the file name (`TrafficMonitor/PluginManager.cpp:50-52`).
5. Checks `theApp.m_cfg_data.plugin_disabled` for the file name; if present, sets `state = PS_DISABLE` and continues (`TrafficMonitor/PluginManager.cpp:54-58`).
6. `LoadLibraryW(plugin_info.file_path)`; on failure sets `PS_MUDULE_LOAD_FAILED` + records `GetLastError` (`TrafficMonitor/PluginManager.cpp:60-66`).
7. `ReplacePluginDrawTextFunction(plugin_info.plugin_module)` to IAT-hook the plugin's `User32` DrawText calls (`TrafficMonitor/PluginManager.cpp:68`); see section 2.5.
8. `GetProcAddress(plugin_info.plugin_module, "TMPluginGetInstance")`; on miss sets `PS_FUNCTION_GET_FAILED` (`TrafficMonitor/PluginManager.cpp:70-76`).
9. Invokes `TMPluginGetInstance()`; if the returned object is null, silently skips (`TrafficMonitor/PluginManager.cpp:78-80`).
10. Calls `plugin->GetAPIVersion()`. Anything `<= PLUGIN_UNSUPPORT_VERSION` (`0`) sets `PS_VERSION_NOT_SUPPORT` (`TrafficMonitor/PluginManager.cpp:14`, `:82-87`).
11. If API version `>= 2`, ensures `<config_dir>\plugins\` exists (`CreateDirectory`) and pushes its path via `OnExtenedInfo(EI_CONFIG_DIR, ...)` (`TrafficMonitor/PluginManager.cpp:88-95`).
12. If API version `>= 7`, calls `OnInitialize(&theApp)` so the plugin receives the `ITrafficMonitor*` (`TrafficMonitor/PluginManager.cpp:97-101`).
13. Reads the six `PluginInfoIndex` slots via `GetInfo` and stores them into `plugin_info.properties` (`TrafficMonitor/PluginManager.cpp:103-108`).
14. Iterates `GetItem(0)`, `GetItem(1)`, ... until a null is returned; each item is appended to `plugin_info.plugin_items`, `m_plugins`, and the reverse map (`TrafficMonitor/PluginManager.cpp:110-121`).
15. After all DLLs are processed, `ReplaceMfcDrawTextFunction()` to IAT-hook the host's own MFC (`TrafficMonitor/PluginManager.cpp:123-124`); see section 2.5.
16. Populates `m_all_display_items_with_plugins` with `AllDisplayItems` (built-in) then every plugin item (`TrafficMonitor/PluginManager.cpp:126-134`).

The destructor frees every `HMODULE` (`TrafficMonitor/PluginManager.cpp:20-25`).

### 2.3 Public query API

| Method | Location | Behavior |
| --- | --- | --- |
| `GetPluginItems()` | `TrafficMonitor/PluginManager.cpp:137-140` | Returns the flat `m_plugins` vector |
| `GetPlugins()` | `TrafficMonitor/PluginManager.cpp:142-145` | Returns the per-DLL `PluginInfo` vector |
| `GetItemById(item_id)` | `TrafficMonitor/PluginManager.cpp:147-155` | Linear scan matching `GetItemId()` |
| `GetItemByIndex(index)` | `TrafficMonitor/PluginManager.cpp:157-162` | Direct index into `m_plugins` |
| `GetItemIndex(item)` | `TrafficMonitor/PluginManager.cpp:164-172` | Reverse index search |
| `GetPluginByItem(pItem)` | `TrafficMonitor/PluginManager.cpp:174-179` | Reverse-map lookup |
| `GetPluginIndex(plugin)` | `TrafficMonitor/PluginManager.cpp:181-189` | Reverse index search in `m_modules` |
| `EnumPlugin(func)` | `TrafficMonitor/PluginManager.cpp:191-200` | Invokes `func` for every `ITMPlugin*` whose `plugin` field is non-null |
| `EnumPluginItem(func)` | `TrafficMonitor/PluginManager.cpp:202-211` | Invokes `func` for every non-null `IPluginItem*` |
| `AllDisplayItemsWithPlugins()` | `TrafficMonitor/PluginManager.cpp:213-216` | Returns the merged `set<CommonDisplayItem>` |
| `GetItemWidth(pItem, pDC)` | `TrafficMonitor/PluginManager.cpp:219-232` | If plugin API `>= 3`, prefers `GetItemWidthEx(hDC)`; otherwise falls back to `theApp.DPI(GetItemWidth())` |

### 2.4 `PluginState` — what each state means

Defined at `TrafficMonitor/PluginManager.h:14-21`. Concrete drivers:

| State | Set at | Reason |
| --- | --- | --- |
| `PS_SUCCEED` | Initialized to default-constructed `PluginInfo` (no explicit assignment); items are populated if the DLL survives every prior gate | Default value, signals a fully loaded plugin |
| `PS_MUDULE_LOAD_FAILED` | `TrafficMonitor/PluginManager.cpp:63` | `LoadLibrary` returned `NULL`; `error_code` holds `GetLastError` |
| `PS_FUNCTION_GET_FAILED` | `TrafficMonitor/PluginManager.cpp:73` | `TMPluginGetInstance` export missing |
| `PS_VERSION_NOT_SUPPORT` | `TrafficMonitor/PluginManager.cpp:85` | `GetAPIVersion() <= 0` |
| `PS_DISABLE` | `TrafficMonitor/PluginManager.cpp:56` | User-disabled via `m_cfg_data.plugin_disabled` |

The states are surfaced to the user as strings via `CPluginManagerDlg::ShowPluginList` (`TrafficMonitor/PluginManagerDlg.cpp:155-164`, mapping each state to a localized `IDS_*` text).

### 2.5 IAT hooking: `ReplacePluginDrawTextFunction` and `ReplaceMfcDrawTextFunction`

Both are private static methods of `CPluginManager`, declared at `TrafficMonitor/PluginManager.h:61-62` and implemented at `TrafficMonitor/PluginManager.cpp:410-474`. The goal is to redirect `User32!DrawTextA`/`DrawTextW`/`DrawTextExA`/`DrawTextExW` calls made by the plugin (and by MFC) into `User32DrawTextManager` so the host can render text correctly under its own DPI / theme settings. The hook is applied to plugin DLLs first (during `LoadPlugins`), then to `mfc140u.dll` once for the host.

Mechanism summary (`TrafficMonitor/PluginManager.cpp:410-464`):

- Walks the PE import directory of the given module manually using `IMAGE_DOS_HEADER` / `IMAGE_NT_HEADERS` / `IMAGE_IMPORT_DESCRIPTOR`.
- For each imported DLL name, matches (case-insensitive) against `User32.dll` via the helper `DllMap` (`TrafficMonitor/PluginManager.cpp:376-406`).
- For each matching DLL, walks its IAT (`IMAGE_THUNK_DATA`) and matches function pointers against `DrawTextA`/`DrawTextW`/`DrawTextExA`/`DrawTextExW` using `DllName::MatchFunctionPointer` (`TrafficMonitor/PluginManager.cpp:362-371`).
- When a match is found, calls `OnFunctionFind(target_pointer, nullptr)` which invokes the replacement handler stored in `User32DrawTextManager` (`TrafficMonitor/PluginManager.cpp:430-433`).

`ReplaceMfcDrawTextFunction` (`TrafficMonitor/PluginManager.cpp:466-474`) just `LoadLibraryA("mfc140u.dll")`, calls the same hook routine, then `FreeLibrary` (the hook targets survive the free because it modifies the in-memory IAT).

---

## 3. Call sites — when does the host invoke each ITMPlugin callback?

| Callback | Call site(s) | Triggered by |
| --- | --- | --- |
| `OnInitialize(ITrafficMonitor*)` | `TrafficMonitor/PluginManager.cpp:100` | Once during `LoadPlugins`, only if `GetAPIVersion() >= 7` |
| `DataRequired()` | `TrafficMonitor/TrafficMonitorDlg.cpp:1504` | Every monitor-data tick (handler is `CTrafficMonitorDlg::OnMonitorInfoUpdated`, which is bound to `WM_MONITOR_INFO_UPDATED` at `TrafficMonitor/TrafficMonitorDlg.cpp:113`) |
| `OnMonitorInfo(monitor_info)` | `TrafficMonitor/TrafficMonitorDlg.cpp:1517` | Same tick, immediately after `DataRequired()` — the host populates a fresh `MonitorInfo` struct from `m_out_speed`, `m_in_speed`, `m_cpu_usage`, ..., `m_main_board_temperature` (`:1505-1516`) |
| `GetTooltipInfo()` | `TrafficMonitor/TrafficMonitor.cpp:1215` | Inside `CTrafficMonitorApp::GetPlauginTooltipInfo`, invoked when the tray tooltip is built (only when `GetAPIVersion() >= 2`) |
| `OnExtenedInfo(EI_DRAW_TASKBAR_WND, ...)` | `TrafficMonitor/TaskBarDlg.cpp:424`, `TrafficMonitor/SkinFile.cpp:521`, `:678` | Tells the plugin whether it is rendering into the taskbar window (`L"1"`) or into a skin context (`L"0"`) |
| `OnExtenedInfo(EI_LABEL_TEXT_COLOR / EI_VALUE_TEXT_COLOR, ...)` | `TrafficMonitor/TaskBarDlg.cpp:434-435`, `TrafficMonitor/SkinFile.cpp:528`, `:686` | Color pushed to the plugin just before `DrawItem` |
| `OnExtenedInfo(EI_*_WND_* flags, ...)` | `TrafficMonitor/TrafficMonitor.cpp:1247-1261` (inside `SendSettingsToPlugin`) | Bulk push of every net-speed / unit / spacing option to every plugin (only when `GetAPIVersion() >= 2`) |
| `OnExtenedInfo(EI_CONFIG_DIR, ...)` | `TrafficMonitor/PluginManager.cpp:94` | Once during `LoadPlugins`, only when `GetAPIVersion() >= 2` |
| `ShowOptionsDialog(hParent)` | `TrafficMonitor/PluginManagerDlg.cpp:257`, `TrafficMonitor/TrafficMonitorDlg.cpp:2959`, `:2993`, `TrafficMonitor/Test.cpp:52` | Plugin manager dialog's "Options" button, and the per-item "Options" entries in the main / taskbar right-click menus |
| `GetInfo(PluginInfoIndex)` | `TrafficMonitor/PluginManager.cpp:107` | Once during `LoadPlugins`, fills `PluginInfo::properties` |
| `GetPluginIcon()` | `TrafficMonitor/TrafficMonitorDlg.cpp:2030`, `TrafficMonitor/TaskBarDlg.cpp:1094` | Just-in-time, when the right-click menu opens (only when `GetAPIVersion() >= 5`) |
| `GetCommandCount()` | `TrafficMonitor/TrafficMonitor.cpp:1279` | Inside `CTrafficMonitorApp::UpdatePluginMenu` (only when `GetAPIVersion() >= 5`) |
| `GetCommandName(i)` | `TrafficMonitor/TrafficMonitor.cpp:1285` | Same menu build |
| `GetCommandIcon(i)` | `TrafficMonitor/TrafficMonitor.cpp:1290` | Same menu build |
| `IsCommandChecked(i)` | `TrafficMonitor/TrafficMonitorDlg.cpp:2253`, `TrafficMonitor/TaskBarDlg.cpp:1147` | When the right-click menu is being shown, to set the check mark |
| `OnPluginCommand(i, hWnd, para)` | `TrafficMonitor/TrafficMonitorDlg.cpp:2199`, `TrafficMonitor/TaskBarDlg.cpp:1286`, `TrafficMonitor/PluginManagerDlg.cpp:362` | When the user picks a plugin command in the menu (only when `GetAPIVersion() >= 5`) |
| `GetItem(index)` | `TrafficMonitor/PluginManager.cpp:114` | During `LoadPlugins` only |
| `GetAPIVersion()` | `TrafficMonitor/PluginManager.cpp:82`, and many `if (plugin->GetAPIVersion() >= N)` guards throughout the codebase | Version-gate before every optional callback |

The `MouseEvent` and `KeboardEvent` calls (per-item, not per-plugin) are at `TrafficMonitor/TrafficMonitorDlg.cpp:1992` (main window right-click), `:2054` (main window left-click), `TrafficMonitor/TaskBarDlg.cpp:1076` (taskbar right-click), `:1177` (taskbar keydown), `:1200` (taskbar double-click) — all gated by the plugin's API version.

---

## 4. Plugin commands — menu integration

The host maintains a generic command-id window reserved for plugins:

- `ID_PLUGIN_COMMAND_START` and `ID_PLUGIN_COMMAND_MAX` are the start/end of a command-id range. The dynamic count is supplied by the plugin via `GetCommandCount()`.

Two menus are involved:

- `m_main_menu_plugin` (resource `IDR_MENU1`), loaded at `TrafficMonitor/TrafficMonitor.cpp:786`. Used when the user right-clicks a plugin item in the **main window** (`TrafficMonitor/TrafficMonitorDlg.cpp:1997`).
- `m_taskbar_menu_plugin` (resource `IDR_TASK_BAR_MENU`), loaded at `TrafficMonitor/TrafficMonitor.cpp:788`. Used when the user right-clicks a plugin item in the **taskbar window** (`TrafficMonitor/TaskBarDlg.cpp:1083`).

Each of these has a dedicated sub-menu (`m_main_menu_plugin_sub_menu` / `m_taskbar_menu_plugin_sub_menu`) for the plugin's own commands. Initial entries (`ID_PLUGIN_OPTIONS`, `ID_PLUGIN_DETAIL`, and their taskbar siblings) are appended at `TrafficMonitor/TrafficMonitor.cpp:791-795` and `:801-804`.

### 4.1 Right-click rebuild sequence

Main window — `CTrafficMonitorDlg::OnRButtonUp` (`TrafficMonitor/TrafficMonitorDlg.cpp:1981-2041`):

1. `CheckClickedItem(point)` — records which item is under the cursor.
2. If a plugin item was hit and `GetAPIVersion() >= 3`, forwards `MT_RCLICKED` to `OnMouseEvent`; non-zero return aborts (`:1985-1994`).
3. Chooses menu: `m_main_menu_plugin.GetSubMenu(0)` if a plugin item was clicked, else the normal menu (`:1997`).
4. Modifies the menu's `<plugin name>` placeholder to display the plugin's `GetInfo(TMI_NAME)` and decorates with `GetPluginIcon()` if available (`:2023-2034`).
5. Calls `theApp.UpdatePluginMenu(&theApp.m_main_menu_plugin_sub_menu, plugin, 2)` to refresh the command list (`:2036`).

Taskbar window — `CTaskBarDlg::OnRButtonUp` (`TrafficMonitor/TaskBarDlg.cpp:1075-1104`): same flow, using `m_taskbar_menu_plugin` and the `KF_TASKBAR_WND` / `MF_TASKBAR_WND` flags.

### 4.2 `CTrafficMonitorApp::UpdatePluginMenu`

`TrafficMonitor/TrafficMonitor.cpp:1266-1296`. Removes any plugin command entries past `plugin_cmd_start_index` (the static `ID_PLUGIN_OPTIONS` / `ID_PLUGIN_DETAIL` items) and rebuilds:

1. Truncates `pMenu` down to `plugin_cmd_start_index` entries.
2. If plugin API `>= 5`, reads `GetCommandCount()`. If non-zero, appends a separator.
3. For each `i in [0, count)`, appends `MF_STRING | MF_ENABLED` with id `ID_PLUGIN_COMMAND_START + i` and label `GetCommandName(i)`; if `GetCommandIcon(i)` returns non-null, attaches the icon via `CMenuIcon::AddIconToMenuItem`.

### 4.3 Command dispatch

Three places handle `ID_PLUGIN_COMMAND_START..ID_PLUGIN_COMMAND_MAX`:

- Main window — `CTrafficMonitorDlg::OnCommand` (`TrafficMonitor/TrafficMonitorDlg.cpp:2190-2202`): resolves `index = uMsg - ID_PLUGIN_COMMAND_START`, gets the plugin from the clicked item via `GetPluginByItem`, requires `GetAPIVersion() >= 5`, then `OnPluginCommand(index, hWnd, nullptr)`.
- Taskbar window — `CTaskBarDlg::OnCommand` (`TrafficMonitor/TaskBarDlg.cpp:1277-1289`): same logic, gated on `m_clicked_item.IsPlugin()`.
- Plugin manager dialog — `CPluginManagerDlg::OnCommand` (`TrafficMonitor/PluginManagerDlg.cpp:350-368`): uses `m_item_selected` instead of the clicked item.

### 4.4 Check-state sync

`CTrafficMonitorDlg::OnInitMenu` (`TrafficMonitor/TrafficMonitorDlg.cpp:2247-2256`) and `CTaskBarDlg::OnInitMenu` (`TrafficMonitor/TaskBarDlg.cpp:1141-1150`) iterate the same id range and call `CheckMenuItem(i, MF_BYCOMMAND | (IsCommandChecked(...) ? MF_CHECKED : MF_UNCHECKED))`.

---

## 5. Custom draw vs. host-rendered text

Every plugin item is drawn in one of two paths, picked by `IsCustomDraw()`:

### 5.1 Host-rendered (default, `IsCustomDraw() == false`)

Host calls `GetItemLableText()` and `GetItemValueText()` to compose the row, then draws them with its own `IDrawCommon`. Width is sized using `GetItemValueSampleText()`.

Taskbar text widths — `CTaskBarDlg::CalculateWidth` (`TrafficMonitor/TaskBarDlg.cpp:820-857`). For a non-custom plugin item the host measures:
- `label_width = m_pDC->GetTextExtent(theApp.m_taskbar_data.disp_str.GetConst(plugin))` (`:843-844`)
- `value_width = m_pDC->GetTextExtent(plugin->GetItemValueSampleText())` (`:845`)

Skin rendering for plugin items — `CSkinFile::DrawItemsInfo` (`TrafficMonitor/SkinFile.cpp:615-699`). At `:698-702` the label uses `theApp.m_main_wnd_data.disp_str.GetConst(plugin_item)` and the value uses `plugin_item->GetItemValueSampleText()`.

### 5.2 Plugin self-draw (`IsCustomDraw() == true`)

Host delegates paint to `plugin_item->DrawItem(hDC, x, y, w, h, dark_mode)`. Width is queried via `CPluginManager::GetItemWidth` (`TrafficMonitor/PluginManager.cpp:219-232`), which prefers `GetItemWidthEx` if available (API `>= 3`) and otherwise scales `GetItemWidth` by DPI. The text getters (`GetItemLableText`, `GetItemValueText`, `GetItemValueSampleText`) are bypassed — the comment in `include/PluginInterface.h:46-52` makes this explicit.

Three render call sites:

- **Taskbar window** — `CTaskBarDlg::DrawTaskBarWindow` (around `TrafficMonitor/TaskBarDlg.cpp:422-457`):
  - Pushes `EI_DRAW_TASKBAR_WND = L"1"`, then `EI_LABEL_TEXT_COLOR` + `EI_VALUE_TEXT_COLOR` (`:422-436`).
  - **RTTI dispatch**: if `drawer` is `CDrawCommon`, calls `DrawItem` directly on the GDI DC (`:441-444`). If `drawer` is `CTaskBarDlgDrawCommon` (D2D), uses `ExecuteGdiOperation` to obtain a temporary HDC via `ref_d2d1_drawer.ExecuteGdiOperation(rect, lambda)` and invokes `DrawItem` on it (`:445-456`). Plugins therefore always receive an HDC, even when the host is drawing D2D — this is the only call site that synthesizes an HDC for plugin callbacks.
  - `dark_mode` flag = `background_brightness < 128` (`:430`), based on `m_taskbar_data.back_color`.

- **Skin preview** (`CSkinFile::DrawInfo` first variant, `TrafficMonitor/SkinFile.cpp:521-543`):
  - Pushes `EI_DRAW_TASKBAR_WND = L"0"`, then `EI_VALUE_TEXT_COLOR` only (no label color, since the label is not drawn by the plugin in this path).
  - `dark_mode` flag = `brightness >= 128` (`:531`) — **opposite polarity** to the taskbar window. This is a known asymmetry.
  - `draw.GetDC()->SetTextColor(cl)` is set on the GDI+ drawer before `DrawItem`, but plugins do not have to honor it (custom draw owns its own rendering).
  - Coordinates passed: `point.x, point.y, layout_item.width, m_layout_info.text_height` — `point` is already offset by `pos.x/pos.y` (preview canvas offset, `:516`).

- **Skin in actual window** — `CSkinFile::DrawItemsInfo` (`TrafficMonitor/SkinFile.cpp:670-691`):
  - Pushes `EI_DRAW_TASKBAR_WND = L"0"` + `EI_VALUE_TEXT_COLOR` (same shape as preview).
  - `dark_mode` flag = `brightness >= 128` (same as preview).
  - Coordinates passed: `layout_item.x, layout_item.y, layout_item.width, m_layout_info.text_height` — **absolute window coordinates**, not offset.
  - Adds explicit `SetBkMode(TRANSPARENT)` on the GDI+ DC before `DrawItem` (`:689`), which the preview path does not.
  - This is the path actually drawn into the main window during normal use.

**Asymmetry summary**:

| Site | `EI_DRAW_TASKBAR_WND` | Color pushed | Coords | `dark_mode` polarity |
| --- | --- | --- | --- | --- |
| Taskbar window | `L"1"` | label + value | `rect.left/top/Width/Height` | `brightness < 128` |
| Skin preview | `L"0"` | value only | `point.x/y` (offset) | `brightness >= 128` |
| Skin in window | `L"0"` | value only | `layout_item.x/y` (absolute) | `brightness >= 128` |

**Contract-level notes (per grill resolution 2026-06-14):**

- The `dark_mode` parameter is documented at `include/PluginInterface.h:73` as "深色模式为 true，浅色模式为 false" — the contract is the semantic name, not a specific threshold. The two polarities observed across call sites are an implementation inconsistency, not a documented contract; plugin authors should branch on `dark_mode` directly, not on internal thresholds.
- The `hDC` parameter is always a `void*` cast of an `HDC` (see `TaskBarDlg.cpp:442`, `:450`, `SkinFile.cpp:531`, `:690`). The host always provides an HDC; when the underlying renderer is D2D/GDI+, the host synthesizes an HDC via `ExecuteGdiOperation` or `Graphics::GetHDC`. Plugins can write HDC-semantic code uniformly.
- `EI_LABEL_TEXT_COLOR` is only pushed in the taskbar path; plugins rendering into a skin context receive only `EI_VALUE_TEXT_COLOR` and must read label color from `m_main_wnd_data` if needed.

### 5.3 Resource bar

When `IsDrawResourceUsageGraph() != 0`, the host reads `GetResourceUsageGraphValue()` per tick and draws a 0..1 progress bar after the value text. The width/preceding layout uses the same code path as text-mode (`TrafficMonitor/TaskBarDlg.cpp:820-857`); the bar rendering lives in the same `DrawTaskBarWindow` flow but uses `GetResourceUsageGraphValue()` instead of text width.

---

## 6. `ExtendedInfoIndex` — full inventory of cross-cuts

Already enumerated in section 1.2. The following is the call-site map for each, summarizing what the host pushes and when:

| Index | Push call site | Trigger |
| --- | --- | --- |
| `EI_LABEL_TEXT_COLOR` | `TrafficMonitor/TaskBarDlg.cpp:434` (and via `EI_VALUE_TEXT_COLOR` in skin contexts) | Just before each `DrawItem` in the taskbar window |
| `EI_VALUE_TEXT_COLOR` | `TrafficMonitor/TaskBarDlg.cpp:435`, `TrafficMonitor/SkinFile.cpp:528`, `:686` | Just before each `DrawItem` |
| `EI_DRAW_TASKBAR_WND` | `TrafficMonitor/TaskBarDlg.cpp:424` (`L"1"`), `TrafficMonitor/SkinFile.cpp:521`, `:678` (`L"0"`) | Per-draw, signals target surface |
| `EI_NAIN_WND_NET_SPEED_SHORT_MODE` | `TrafficMonitor/TrafficMonitor.cpp:1247` | In `SendSettingsToPlugin` (bulk) |
| `EI_MAIN_WND_SPERATE_WITH_SPACE` | `TrafficMonitor/TrafficMonitor.cpp:1248` | In `SendSettingsToPlugin` |
| `EI_MAIN_WND_UNIT_BYTE` | `TrafficMonitor/TrafficMonitor.cpp:1249` | In `SendSettingsToPlugin` |
| `EI_MAIN_WND_UNIT_SELECT` | `TrafficMonitor/TrafficMonitor.cpp:1250` | In `SendSettingsToPlugin` |
| `EI_MAIN_WND_NOT_SHOW_UNIT` | `TrafficMonitor/TrafficMonitor.cpp:1251` | In `SendSettingsToPlugin` |
| `EI_MAIN_WND_NOT_SHOW_PERCENT` | `TrafficMonitor/TrafficMonitor.cpp:1252` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_NET_SPEED_SHORT_MODE` | `TrafficMonitor/TrafficMonitor.cpp:1254` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_SPERATE_WITH_SPACE` | `TrafficMonitor/TrafficMonitor.cpp:1255` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_VALUE_RIGHT_ALIGN` | `TrafficMonitor/TrafficMonitor.cpp:1256` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_NET_SPEED_WIDTH` | `TrafficMonitor/TrafficMonitor.cpp:1257` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_UNIT_BYTE` | `TrafficMonitor/TrafficMonitor.cpp:1258` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_UNIT_SELECT` | `TrafficMonitor/TrafficMonitor.cpp:1259` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_NOT_SHOW_UNIT` | `TrafficMonitor/TrafficMonitor.cpp:1260` | In `SendSettingsToPlugin` |
| `EI_TASKBAR_WND_NOT_SHOW_PERCENT` | `TrafficMonitor/TrafficMonitor.cpp:1261` | In `SendSettingsToPlugin` |
| `EI_CONFIG_DIR` | `TrafficMonitor/PluginManager.cpp:94` | Once during `LoadPlugins` |

`SendSettingsToPlugin` itself is invoked by the host whenever the user changes any of those settings — its callers live outside the file but its body at `TrafficMonitor/TrafficMonitor.cpp:1241-1264` is the only producer for the bulk-pushed indices above.

---

## 7. Plugin updates: `CPluginUpdateHelper`

Source: `TrafficMonitor/PluginUpdateHelper.h` (36 lines), `TrafficMonitor/PluginUpdateHelper.cpp` (123 lines).

### 7.1 What it is

A small companion to `CUpdateHelper` (`TrafficMonitor/UpdateHelper.h:2-41`) that fetches and parses a remote XML manifest of plugin versions. The version info lives at two URLs, selected by the user's `update_source` setting (`TrafficMonitor/PluginUpdateHelper.cpp:88-92`):

- GitHub (default): `https://raw.githubusercontent.com/zhongyang219/TrafficMonitorPlugins/main/plugins_version.xml`
- Gitee: `https://gitee.com/zhongyang219/TrafficMonitorPlugins/raw/main/plugins_version.xml`

### 7.2 `CheckForUpdate()` — `TrafficMonitor/PluginUpdateHelper.cpp:83-111`

1. Clears `m_latest_versions`.
2. Picks URL from `theApp.m_general_data.update_source` (`0`=GitHub, `1`=Gitee).
3. `CCommon::GetURL(url, version_info)` downloads the XML as `std::string`.
4. Parses with tinyxml2 (`doc.Parse`), iterates child nodes of the root.
5. For each `<plugin file_name="X" version="Y" .../>`, builds `PluginVersion(version_str)` and inserts into `m_latest_versions[file_name]`.

### 7.3 `GetPluginLatestVersions(file_name)` — `TrafficMonitor/PluginUpdateHelper.cpp:113-122`

Returns the cached `PluginVersion` for the given file name (the file name is the same DLL file name as `PluginInfo::file_path`). Misses return a static empty `PluginVersion`.

### 7.4 `PluginVersion` — `TrafficMonitor/PluginUpdateHelper.h:3-22`, impl `TrafficMonitor/PluginUpdateHelper.cpp:29-80`

Dot-separated integer list with `<` and `==` operators that compare element-wise (zero-padding shorter sequences via `GetSubVersion`, `:75-80`). `GetVersionString`/`GetVersionWString` return the original string.

### 7.5 Consumer

`CPluginManagerDlg::ShowPluginList` (`TrafficMonitor/PluginManagerDlg.cpp:171-181`) compares the plugin's reported `TMI_VERSION` against `m_plugin_update.GetPluginLatestVersions(file_name)`. When the cached version is newer, the manager UI appends `(new version X.Y.Z)` next to the existing version text. The current data is held by `CTrafficMonitorApp::m_plugin_update` (`TrafficMonitor/TrafficMonitor.h:92`).

### 7.6 Difference from `CUpdateHelper`

| Aspect | `CUpdateHelper` (main app) | `CPluginUpdateHelper` |
| --- | --- | --- |
| Scope | Main TrafficMonitor binaries | Each plugin DLL individually |
| Data source | Different XML (main app release manifest) | `plugins_version.xml` at the URLs above |
| Result type | Stores version + multi-arch download links + changelog strings (`TrafficMonitor/UpdateHelper.h:31-38`) | Stores `map<wstring, PluginVersion>` of file-name to latest version (`TrafficMonitor/PluginUpdateHelper.h:33`) |
| Update channel class | `UpdateSource` enum (`GitHubSource`/`GiteeSource`) | Plain `int` switch on `m_general_data.update_source` (`:88-92`) |
| Public surface | `SetUpdateSource`, `CheckForUpdate`, plus getters for version / links / contents | `CheckForUpdate`, `GetPluginLatestVersions` |
| Purpose | Downloads new main-app binaries | Surfaces "an update is available" hint in the plugin manager dialog (does not download anything) |

---

## 8. Plugin lifecycle summary

1. **Discovery** — `CPluginManager::LoadPlugins` enumerates `plugins/*.dll`.
2. **Skip** — disabled-by-config files short-circuit to `PS_DISABLE`.
3. **Load** — `LoadLibrary` (failure → `PS_MUDULE_LOAD_FAILED`).
4. **IAT hook** — `ReplacePluginDrawTextFunction` redirects `User32!DrawText*`.
5. **Factory** — `TMPluginGetInstance` resolves (failure → `PS_FUNCTION_GET_FAILED`).
6. **Version gate** — `GetAPIVersion() <= 0` → `PS_VERSION_NOT_SUPPORT`.
7. **Config dir push** — `OnExtenedInfo(EI_CONFIG_DIR)` (API `>= 2`).
8. **Initialize** — `OnInitialize(&theApp)` (API `>= 7`).
9. **Info harvest** — `GetInfo` for all six `PluginInfoIndex` slots, cached into `PluginInfo::properties`.
10. **Item harvest** — `GetItem(0)`, `GetItem(1)`, ... until null.
11. **MFC hook** — `ReplaceMfcDrawTextFunction` for host's own MFC.
12. **Item registration** — flat `m_plugins`, reverse map `m_plguin_item_map`, merged display set.
13. **Runtime tick** — every monitor-data tick: `DataRequired` then `OnMonitorInfo`. Optional mouse / keyboard events forwarded per item. Optional tooltip via `GetTooltipInfo`.
14. **Menu refresh** — on right-click, `UpdatePluginMenu` rebuilds the command list using `GetCommandCount`/`GetCommandName`/`GetCommandIcon`; check state via `IsCommandChecked`. Dispatch via `OnPluginCommand`.
15. **Draw** — per-item paint, host-rendered or self-drawn depending on `IsCustomDraw`. Color / drawing-target flags pushed via `OnExtenedInfo` immediately before each draw.
16. **Options** — `ShowOptionsDialog` invoked from main-window / taskbar-window right-click menus, or from the plugin manager dialog.
17. **Update hint** — `CPluginUpdateHelper::GetPluginLatestVersions` is read by the plugin manager UI to annotate rows.
18. **Unload** — destructor calls `FreeLibrary` on each `HMODULE` (`TrafficMonitor/PluginManager.cpp:23-24`).