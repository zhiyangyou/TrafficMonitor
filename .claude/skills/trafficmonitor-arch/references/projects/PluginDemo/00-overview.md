# PluginDemo — official plugin example

## 1. Role and scope

`PluginDemo/` is a self-contained sample plugin that demonstrates every major feature of the plugin interface. It is not a dependency of `TrafficMonitor/`. The main app loads it the same way it loads any third-party DLL: by scanning `plugins/*.dll`. The solution file `TrafficMonitor.sln` lists it as an independent VS project.

Goal: serve as a copy-pasteable template showing how to implement `ITMPlugin`, how to expose multiple `IPluginItem`s, how to use the reverse `ITrafficMonitor*` interface, and how to combine host-rendered and self-drawn items inside a single DLL.

## 2. File inventory

| File | Role |
| --- | --- |
| `PluginDemo/PluginDemo.cpp` | The `ITMPlugin` implementation, plus the `TMPluginGetInstance` export |
| `PluginDemo/PluginDemo.h` | Declaration of `CPluginDemo` and the `extern "C"` export |
| `PluginDemo/PluginSystemDate.cpp` / `.h` | `IPluginItem` showing host-rendered date display |
| `PluginDemo/PluginSystemTime.cpp` / `.h` | `IPluginItem` showing host-rendered time display + resource bar |
| `PluginDemo/CustomDrawItem.cpp` / `.h` | `IPluginItem` showing `IsCustomDraw() == true` (plugin-owned rendering) |
| `PluginDemo/DataManager.cpp` / `.h` | Singleton holding shared state, configuration loading/saving, string-resource cache |
| `PluginDemo/OptionsDlg.cpp` / `.h` | MFC dialog returned by `ShowOptionsDialog` |
| `PluginDemo/pch.h` / `pch.cpp` | Precompiled header (`framework.h` + `resource.h`) |
| `PluginDemo/framework.h` | Defines `WIN32_LEAN_AND_MEAN`, includes MFC (`afxwin.h`, `afxext.h`, `afxdisp.h`) and Common-Controls manifest |
| `PluginDemo/resource.h` | Resource id definitions (IDS_PLUGIN_NAME, IDS_PLUGIN_DESCRIPTION, IDS_DATE, IDS_TIME, IDS_CUSTOM_DRAW_ITEM, IDD_OPTIONS_DIALOG, IDC_SHOW_SECOND_CHECK) |
| `PluginDemo/PluginDemo.rc` | Resource script bound to `resource.h` |
| `PluginDemo/PluginDemo.vcxproj` | MSBuild project (see "Build" below) |
| `PluginDemo/PluginDemo.vcxproj.filters` | VS filter view (does not affect build) |

## 3. The three sample items

### 3.1 `CPluginDemo` — the ITMPlugin implementation

Declaration: `PluginDemo/PluginDemo.h:7-30`. Implementation: `PluginDemo/PluginDemo.cpp`.

Key facts:

- Inherits from `ITMPlugin` (`PluginDemo/PluginDemo.h:7`).
- Holds three items as members: `m_system_date`, `m_system_time`, `m_custom_draw_item` (`PluginDemo/PluginDemo.h:24-26`).
- Implements API version 7 (inherits `ITMPlugin::GetAPIVersion()` default `7`).
- Stores the host `ITrafficMonitor*` in `m_app` (`:27`) for later use.
- Uses a Meyer's-singleton instance: `static CPluginDemo m_instance` (`:29`), accessed via `Instance()` (`PluginDemo/PluginDemo.cpp:12-15`).

Per-interface implementation:

| Method | Location | Behavior |
| --- | --- | --- |
| `GetItem(index)` | `PluginDemo/PluginDemo.cpp:17-31` | Switch: `0` returns date item, `1` time item, `2` custom-draw item, otherwise `nullptr` |
| `DataRequired()` | `PluginDemo/PluginDemo.cpp:33-47` | Calls `GetLocalTime(&system_time)` (cached into `CDataManager::Instance().m_system_time`), then formats `m_cur_date` (`YYYY/MM/DD`) and `m_cur_time` (`HH:MM:SS` or `HH:MM` based on `show_second` flag) |
| `GetInfo(index)` | `PluginDemo/PluginDemo.cpp:49-74` | Pulls name/description from resource string (`IDS_PLUGIN_NAME`, `IDS_PLUGIN_DESCRIPTION`), hard-codes author `zhongyang219`, copyright `Copyright (C) by Zhong Yang 2021`, version `1.0`, URL `https://github.com/zhongyang219/TrafficMonitor`. Uses `AFX_MANAGE_STATE(AfxGetStaticModuleState())` so resource lookup uses the DLL's instance handle (`:51`) |
| `ShowOptionsDialog(hParent)` | `PluginDemo/PluginDemo.cpp:76-87` | `AFX_MANAGE_STATE`, instantiates `COptionsDlg` with the supplied `hWnd` as parent, copies `CDataManager::Instance().m_setting_data` into the dialog, returns `OR_OPTION_CHANGED` if user pressed OK, otherwise `OR_OPTION_UNCHANGED` |
| `OnExtenedInfo(index, data)` | `PluginDemo/PluginDemo.cpp:90-102` | On `EI_CONFIG_DIR`, calls `g_data.LoadConfig(data)` (the `g_data` macro at `PluginDemo/DataManager.h:5`) |
| `OnInitialize(pApp)` | `PluginDemo/PluginDemo.cpp:104-110` | Stores `pApp` in `m_app`, then demonstrates reverse-interface use by calling `m_app->GetStringRes(L"IDS_MEMORY_USAGE", L"text")` and `m_app->GetStringRes(L"BCP_47", L"general")`. Return values are not actually used (`int a = 0;`) — it is a smoke-test that the round trip works |

The export: `PluginDemo/PluginDemo.cpp:112-116`

```cpp
ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CPluginDemo::Instance();
}
```

`AFX_MANAGE_STATE` is required because MFC looks up resources / message maps using the calling thread's state; this switches to the DLL's module state so `COptionsDlg` and the string table resolve correctly.

### 3.2 `CPluginSystemDate` — host-rendered IPluginItem (text)

Declaration: `PluginDemo/PluginSystemDate.h:4-17`. Implementation: `PluginDemo/PluginSystemDate.cpp`.

| Method | Location | Value |
| --- | --- | --- |
| `GetItemName()` | `PluginDemo/PluginSystemDate.cpp:10-13` | `CDataManager::Instance().StringRes(IDS_DATE)` |
| `GetItemId()` | `PluginDemo/PluginSystemDate.cpp:15-18` | Hard-coded `uQI0sH6a` (must remain stable across versions because the host persists per-item display by id) |
| `GetItemLableText()` | `PluginDemo/PluginSystemDate.cpp:20-23` | Same as `GetItemName` |
| `GetItemValueText()` | `PluginDemo/PluginSystemDate.cpp:25-28` | `CDataManager::Instance().m_cur_date` (filled by `DataRequired`) |
| `GetItemValueSampleText()` | `PluginDemo/PluginSystemDate.cpp:30-33` | `L"2022/08/08"` — width probe |

`IsCustomDraw()` defaults to `false`; host composes the row from label + value.

### 3.3 `CPluginSystemTime` — host-rendered IPluginItem (text + resource bar)

Declaration: `PluginDemo/PluginSystemTime.h:4-20`. Implementation: `PluginDemo/PluginSystemTime.cpp`.

Adds two functions on top of the date item:

- `IsDrawResourceUsageGraph()` at `PluginDemo/PluginSystemTime.cpp:38-41` returns `1` — host will paint the small resource bar.
- `GetResourceUsageGraphValue()` at `PluginDemo/PluginSystemTime.cpp:43-47` returns `m_system_time.wSecond / 60.0f` — the resource bar rises within the current minute, resets every 60 seconds.

Sample text adapts to the user setting: `12:00:00` when `show_second`, otherwise `12:00` (`PluginDemo/PluginSystemTime.cpp:30-36`).

### 3.4 `CCustomDrawItem` — self-drawn IPluginItem

Declaration: `PluginDemo/CustomDrawItem.h:4-15`. Implementation: `PluginDemo/CustomDrawItem.cpp`.

| Method | Location | Behavior |
| --- | --- | --- |
| `GetItemName()` | `PluginDemo/CustomDrawItem.cpp:5-8` | `IDS_CUSTOM_DRAW_ITEM` |
| `GetItemId()` | `PluginDemo/CustomDrawItem.cpp:10-13` | Hard-coded `4Tc21hGS` |
| `GetItemLableText()` / `GetItemValueText()` / `GetItemValueSampleText()` | `PluginDemo/CustomDrawItem.cpp:15-28` | All return `L""` — irrelevant because the item is self-drawn |
| `IsCustomDraw()` | `PluginDemo/CustomDrawItem.cpp:30-33` | Returns `true` |
| `GetItemWidth()` | `PluginDemo/CustomDrawItem.cpp:35-38` | Returns `50` (at 96 DPI; host scales) |
| `DrawItem(hDC, x, y, w, h, dark_mode)` | `PluginDemo/CustomDrawItem.cpp:50-88` | Draws a tri-band horizontal bar: orange for hours-of-day, green for minutes-of-hour, blue for seconds-of-minute. Tick marks every hour (longer every 6th). Color choices flip between dark and light mode (`:57-59`). Uses `CDC::FillSolidRect` and a private `DrawLine` helper (`:40-48`). |

Because `IsCustomDraw()` is `true`, the host's `GetItemWidthEx` query at `TrafficMonitor/PluginManager.cpp:225` would be tried first — since `CCustomDrawItem` does not override `GetItemWidthEx`, the host falls back to `GetItemWidth() * DPI` (`TrafficMonitor/PluginManager.cpp:227-230`).

### 3.5 `CDataManager` — shared state, config, and string cache

Declaration: `PluginDemo/DataManager.h:13-36`. Implementation: `PluginDemo/DataManager.cpp`.

- Singleton via `m_instance` (`PluginDemo/DataManager.cpp:4`, `PluginDemo/DataManager.h:33`).
- Members (`PluginDemo/DataManager.h:26-30`): `m_cur_time` (wstring), `m_cur_date` (wstring), `m_system_time` (SYSTEMTIME), `m_setting_data` (struct with `bool show_second`).
- `LoadConfig(config_dir)` at `PluginDemo/DataManager.cpp:20-38`: derives the module's file path via `__ImageBase` and `GetModuleFileNameW`, then if a `config_dir` was supplied (`EI_CONFIG_DIR`), builds `<config_dir><module_file_name>.ini` as `m_config_path`. Reads `show_second` via `GetPrivateProfileInt`.
- `SaveConfig()` at `PluginDemo/DataManager.cpp:47-51`: writes `show_second` via the local `WritePrivateProfileInt` helper. Called from the destructor (`:11-13`), so values persist when the DLL unloads.
- `StringRes(id)` at `PluginDemo/DataManager.cpp:53-66`: lazy `LoadString` cache keyed by resource id; uses `AFX_MANAGE_STATE` so resources load from the DLL.

### 3.6 `COptionsDlg` — minimal MFC dialog

Declaration: `PluginDemo/OptionsDlg.h:8-28`. Implementation: `PluginDemo/OptionsDlg.cpp`.

- `IDD_OPTIONS_DIALOG` template, single checkbox `IDC_SHOW_SECOND_CHECK`.
- `OnInitDialog` (`PluginDemo/OptionsDlg.cpp:38-49`) sets the check state from `m_data.show_second`.
- `OnBnClickedShowSecondCheck` (`PluginDemo/OptionsDlg.cpp:52-56`) toggles `m_data.show_second`.
- `m_data` is a public `SettingData` member (`PluginDemo/OptionsDlg.h:19`) — the plugin reads/writes it before and after `DoModal()` in `ShowOptionsDialog` (`PluginDemo/PluginDemo.cpp:80-86`).

## 4. How the demo exercises every API surface

| Interface | Surface | Where demonstrated |
| --- | --- | --- |
| `ITMPlugin` | `GetItem` | `PluginDemo.cpp:17-31` |
| `ITMPlugin` | `DataRequired` | `PluginDemo.cpp:33-47` |
| `ITMPlugin` | `GetInfo` (all six slots) | `PluginDemo.cpp:49-74` |
| `ITMPlugin` | `ShowOptionsDialog` | `PluginDemo.cpp:76-87` |
| `ITMPlugin` | `OnExtenedInfo` (config dir) | `PluginDemo.cpp:90-102` |
| `ITMPlugin` | `OnInitialize` | `PluginDemo.cpp:104-110` |
| `IPluginItem` | text path (label + value + sample) | `PluginSystemDate.cpp` and `PluginSystemTime.cpp` |
| `IPluginItem` | `IsDrawResourceUsageGraph` + `GetResourceUsageGraphValue` | `PluginSystemTime.cpp:38-47` |
| `IPluginItem` | `IsCustomDraw` + `DrawItem` + `GetItemWidth` | `CustomDrawItem.cpp:30-88` |
| `ITrafficMonitor` (reverse) | `GetStringRes` | `PluginDemo.cpp:107-108` |
| `TMPluginGetInstance` export | factory function | `PluginDemo.cpp:112-116` |

What the demo does **not** demonstrate: `OnMonitorInfo`, `GetTooltipInfo`, `GetPluginIcon`, `GetCommandCount` / `GetCommandName` / `GetCommandIcon` / `OnPluginCommand` / `IsCommandChecked`, `OnMouseEvent`, `OnKeboardEvent`, `GetItemWidthEx`. A plugin author writing these would need to add them; the host-side wiring is documented in `skill/topics/plugin-system.md`.

## 5. Build and deployment

### 5.1 Project type

`PluginDemo/PluginDemo.vcxproj:39-81` declares `<ConfigurationType>DynamicLibrary</ConfigurationType>` for all configurations. `<UseOfMfc>Dynamic</UseOfMfc>` — the DLL links against MFC dynamically (consistent with `mfc140u.dll` hook in the host).

Supported configurations (`:4-28`): `Debug|Release` × `Win32|x64|ARM64EC`. `<PlatformToolset>v143</PlatformToolset>` (`:40`, `:47`, etc.).

### 5.2 Output location

`<OutDir>` is defined per configuration at `PluginDemo/PluginDemo.vcxproj:106-135`:

| Configuration | Output directory |
| --- | --- |
| `Debug|Win32` | `$(SolutionDir)Bin\$(Configuration)\plugins\` |
| `Release|Win32` | `$(SolutionDir)Bin\$(Configuration)\plugins\` |
| `Debug|x64` | `$(SolutionDir)Bin\$(Platform)\$(Configuration)\plugins\` |
| `Debug|ARM64EC` | `$(SolutionDir)Bin\$(Platform)\$(Configuration)\plugins\` |
| `Release|x64` | `$(SolutionDir)Bin\$(Platform)\$(Configuration)\plugins\` |
| `Release|ARM64EC` | `$(SolutionDir)Bin\$(Platform)\$(Configuration)\plugins\` |

In all cases the output lands directly in the `plugins\` subdirectory under `Bin\…\`. Because `CPluginManager::LoadPlugins` scans `<exe-dir>\plugins\*.dll` (`TrafficMonitor/PluginManager.cpp:38-41`), a successful build of `PluginDemo` produces a DLL that the next launch of `TrafficMonitor` picks up automatically — no copy step required.

### 5.3 Header dependencies

`<IncludePath>` is set to `$(ProjectDir)..\include;$(IncludePath)` at `PluginDemo/PluginDemo.vcxproj:108-134`. That is what makes `#include "PluginInterface.h` resolve against `../include/PluginInterface.h`. The project also explicitly lists `..\include\PluginInterface.h` in `<ClInclude>` (`:245`) so it shows up in Solution Explorer.

### 5.4 Configuration persistence

At runtime the plugin reads/writes its config from `<exe-config-dir>\plugins\<PluginDemo.dll>.ini` (built in `CDataManager::LoadConfig`, `PluginDemo/DataManager.cpp:27-35`). `EI_CONFIG_DIR` carries that directory into the plugin via `OnExtenedInfo` (`PluginDemo/PluginDemo.cpp:94-97`), exactly as documented in `skill/topics/plugin-system.md` § 1.2.

## 6. Quick mental model for new plugin authors

1. Copy `PluginDemo/` into a sibling directory.
2. Rename `CPluginDemo` and the file/class names.
3. Replace the three `IPluginItem` implementations with your own data sources; keep the `AFX_MANAGE_STATE` macros.
4. Use `CDataManager::Instance()` (or your own equivalent) to share state between `DataRequired` and `GetItemValueText`.
5. If you need user-tweakable settings, follow the `ShowOptionsDialog` → `COptionsDlg` pattern.
6. The host will find the built DLL in `Bin\…\plugins\` automatically. Configuration persists next to it.