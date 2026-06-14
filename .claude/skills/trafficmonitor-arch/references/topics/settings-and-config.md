# Settings and Config（配置与设置）

本文档覆盖 TrafficMonitor 的配置文件位置、INI 读写层、设置对话框族、数据结构映射、开机自启、全局/用户配置区分，以及插件禁用列表的落地。

所有结论来自源码，引用形如 `文件路径:行号`。

---

## 1. 配置文件位置：m_config_path / m_appdata_dir / m_config_dir

路径成员全部在 `CTrafficMonitorApp`（`TrafficMonitor.h:36-45`）：

```
m_module_path      // exe 文件绝对路径（GetModuleFileNameW）
m_module_path_reg  // 写入注册表/任务计划时使用的路径，路径含空格时套双引号
m_module_dir       // exe 所在目录
m_appdata_dir      // %APPDATA%\TrafficMonitor（由 CCommon::GetAppDataConfigDir 返回）
m_config_dir       // 当前使用的"配置目录"
m_config_path      // config.ini 完整路径 = m_config_dir + L"config.ini"
m_history_traffic_path // 历史流量文件 = m_config_dir + L"history_traffic.dat"
m_log_path         // 错误日志 = m_config_dir + L"error.log"
m_skin_path        // 皮肤目录 = m_module_dir + L"skins\\"（始终在 exe 同级，不走 AppData）
```

### 1.1 装配顺序（InitInstance）

`TrafficMonitor.cpp:915-957`：

1. `GetModuleFileNameW(NULL, path, MAX_PATH)` → `m_module_path`；
2. 如果 `m_module_path` 含空格，`m_module_path_reg = L'\"' + m_module_path + L'\"'`，否则原样（`TrafficMonitor.cpp:926-937`）；
3. `m_module_dir = CCommon::GetModuleDir()`，`m_system_dir`、`m_appdata_dir = CCommon::GetAppDataConfigDir()`（`TrafficMonitor.cpp:938-940`）；
4. `LoadGlobalConfig()`（`TrafficMonitor.cpp:942`，详见 §6）—— 决定 `portable_mode` 与 `m_module_dir_writable`；
5. 根据 `portable_mode` 与 `m_module_dir_writable` 决定 `m_config_dir`（`TrafficMonitor.cpp:944-953`）：
   - `_DEBUG`：`m_config_dir = L".\\"`，`m_skin_path = L".\\skins\\"`；
   - Release：`m_config_dir = m_module_dir`（便携模式）或 `m_appdata_dir`（正常）。
   - `m_skin_path = m_module_dir + L"skins\\"` 始终不变。
6. `m_config_path = m_config_dir + L"config.ini"` 等。

### 1.2 读写降级路径

- **正常路径**：`%APPDATA%\TrafficMonitor\config.ini`。
- **便携模式**（用户在"常规设置"里勾选"保存到程序目录"）：`exe 同目录\config.ini`。
- **强制降级**：若 `m_module_dir_writable == false`（`LoadGlobalConfig` 在 exe 所在目录写 `global_cfg.ini` 失败，或 exe 在 Temp 目录下），则 `m_general_data.portable_mode = false`，强制走 AppData（`TrafficMonitor.cpp:477-487`）。
- **首次启动判定**：若 `global_cfg.ini` 不存在，再根据 AppData 下是否已有 `config.ini` 来决定默认是否便携（`TrafficMonitor.cpp:466-470`）。

### 1.3 历史数据迁移占位代码

`TrafficMonitor.cpp:959-968` 存在一段被注释掉的"旧 config.ini/history_traffic.dat 从程序目录移动到 AppData"的迁移代码——目前**未启用**。

## 2. CIniHelper / CSettingsHelper 的角色差异

### 2.1 CIniHelper（底层通用 INI 读写）

定义：`IniHelper.h:7-58`，实现：`IniHelper.cpp`。

- 构造：`CIniHelper(const wstring& file_path, bool force_utf8 = false)` 立即读全文到内存 `m_ini_str`（`IniHelper.cpp:5-43`），UTF-8 BOM 自动识别并删除；支持从资源 `CIniHelper(UINT id, ...)` 加载只读 INI。
- 写操作只修改 `m_ini_str`，**必须显式调 `Save()` 才落盘**（`IniHelper.h:45`）。
- 提供：`WriteString/GetString/WriteInt/GetInt/WriteBool/GetBool/WriteIntArray/WriteBoolArray/WriteStringList/GetStringList` + `GetAllAppName(prefix)` / `GetAllKeyValues` / `RemoveSection` / `SetSaveAsUTF8` / `FromDirectString`（`IniHelper.h:23-44`）。
- 字符串里如果有空格/特定字符，会在写入时自动加引号封装（`IniHelper.cpp:71-78`）。

### 2.2 CSettingsHelper（项目专用高层包装）

定义：`SettingsHelper.h:5-41`，实现：`SettingsHelper.cpp`，**继承自 `CIniHelper`**。

- 默认无参构造（`SettingsHelper.cpp:5-8`）：自动绑定到 `theApp.m_config_path`——绝大多数"读写用户配置"场景直接 `CSettingsHelper ini;` 即可。
- 显式构造（`SettingsHelper.cpp:10-13`）：可传任意 `file_path`，给 `CSkinManager` 等场景用。
- 额外方法（皮肤管理器/任务栏/主窗口共用的"颜色/字体/显示文本"读写对）：
  - `LoadFontData` / `SaveFontData`（`SettingsHelper.cpp:15-37`）：把 `FontInfo` 拆为 `font_name`/`font_size`/`font_style[4]` 4 个 BOOL。
  - `LoadMainWndColors` / `SaveMainWndColors`（`SettingsHelper.cpp:39-71`）：逗号分隔的 `CommonDisplayItem -> COLORREF`。
  - `LoadTaskbarWndColors` / `SaveTaskbarWndColors`（`SettingsHelper.cpp:73-117`）：逗号分隔的 `CommonDisplayItem -> TaskbarItemColor{label,value}`，每项占两个值。
  - `LoadDisplayStr` / `SaveDisplayStr`（`SettingsHelper.cpp:119-140`）：内置 `AllDisplayItems` 的显示文本。主窗口调用 `is_main_window=true`（只读已存在的项），任务栏调 `false`（强制覆盖）。
  - `LoadPluginDisplayStr` / `SavePluginDisplayStr`（`SettingsHelper.cpp:142-161`）：插件项的显示文本，使用 `plugin->GetItemId()` 作为 key。

## 3. config.ini 的 section 划分

下文各 section 与示例键均出自 `TrafficMonitor.cpp:65-287`（LoadConfig）与 `TrafficMonitor.cpp:289-456`（SaveConfig），以及 `TaskbarDefaultStyle.cpp:16-83`、`TrafficMonitor.cpp:458-462`。每个 section 只列典型字段，不展开全部键。

### 3.1 `[general]` —— GeneralSettingData

由 `LoadConfig` 前 1/4 集中读取/写入。典型字段：

- `check_update_when_start`、`update_source`、`show_all_interface`
- `monitor_time_span`、`hard_disk_name`、`cpu_core_name`、`hardware_monitor_item`
- `connections_hide`（字符串列表，写为逗号分隔）
- `language`（由 `m_general_data.language.toConfigString/fromConfigString` 编解码，`CommonData.h:185-187`）

注意：`[general]` 不含 `auto_run`、`portable_mode`——它们属于 `[other]` / `global_cfg.ini`（见 §6）。

### 3.2 `[config]` —— MainWndSettingData + 部分 MainConfigData

主要承载"主窗口"相关的状态字段：

- `transparency`、`always_on_top`、`lock_window_pos`、`mouse_penetrate`、`hide_main_wnd_when_fullscreen`、`hide_main_window`、`swap_up_down`、`alow_out_of_border`
- `show_notify_icon`、`show_task_bar_wnd`、`show_cpu_memory`（即 `m_show_more_info`）
- `position_x`、`position_y`
- `notify_icon_selected`、`notify_icon_auto_adapt`
- 速度单位/格式：`speed_short_mode`、`separate_value_unit_with_space`、`memory_display`、`unit_byte`、`speed_unit`、`hide_unit`、`hide_percent`
- 鼠标双击：`double_click_action`、`double_click_exe`
- 旧版 `show_more_info`、`show_cpu_memory`、`skin_selected`（`m_cfg_data.m_skin_name`）

主窗口的"文本颜色/字体/显示文本"不再从这里读——而是每个皮肤的独立覆盖项，存到 `skin_<皮肤名>` section（见 skin-system 文档）。`LoadConfig` 把 `m_main_wnd_data.text_colors/specify_each_item_color/font/disp_str` 全部初始化为固定值（`TrafficMonitor.cpp:123-131`），运行时由皮肤覆盖项回填。

### 3.3 `[connection]` —— MainConfigData 中连接相关

- `auto_select`、`select_all`、`connection_name`

### 3.4 `[skins]` —— 自适配皮肤名

- `skin_auto_adapt`、`skin_name_dark_mode`、`skin_name_light_mode`

### 3.5 `[notify_tip]` —— GeneralSettingData 中各种超出提示

- `traffic_tip_enable/value/unit`
- `memory_usage_tip_enable`/`memory_tip_value`
- `cpu_temperature_tip_enable/value`、`gpu_temperature_tip_enable/value`、`hdd_temperature_tip_enable/value`、`mainboard_temperature_tip_enable/value`

### 3.6 `[task_bar]` —— TaskBarSettingData + 部分全局字段

任务栏窗口的全部设置都在这里，是 `config.ini` 中最大的一节：

- 颜色：`task_bar_back_color`、`transparent_color`、`status_bar_color`、`task_bar_text_color`（CSV，由 `LoadTaskbarWndColors` 解析）
- 显示项：`tbar_display_item`（int，`DisplayItemSet::ToInt`）、`specify_each_item_color`
- 字体/文本：`SaveFontData` + `SaveDisplayStr`
- 布局：`task_bar_wnd_on_left`、`task_bar_wnd_snap`、`value_right_align`、`horizontal_arrange`、`show_status_bar`、`digits_number`、`item_space`、`vertical_margin`、`window_offset_top/left`、`taskbar_left_space_win11`、`taskbar_right_space_win11`、`avoid_overlap_with_widgets`
- 数值格式：`task_bar_speed_short_mode`、`task_bar_speed_unit`、`task_bar_hide_unit`、`task_bar_hide_percent`、`unit_byte`、`separate_value_unit_with_space`、`show_tool_tip`、`memory_display`、`double_click_action`、`double_click_exe`
- 网速占用图：`show_netspeed_figure`、`netspeed_figure_max_value/unit`、`graph_color_following_system`、`cm_graph_type`、`show_graph_dashed_box`
- 自动适配主题：`auto_adapt_light_theme`、`dark_default_style`、`light_default_style`、`auto_set_background_color`、`auto_save_taskbar_color_settings_to_preset`
- 项顺序与插件项：`item_order`（`CTaskbarItemOrderHelper::ToString`）、`plugin_display_item`（`StringSet::ToString`）
- 副显示器：`show_taskbar_wnd_in_secondary_display`、`secondary_display_index`
- 渲染：`disable_d2d`、`enable_colorful_emoji`

### 3.7 `[taskbar_default_style]` —— TaskbarDefaultStyle 颜色预设

由 `TaskbarDefaultStyle::LoadConfig/SaveConfig` 维护（`TaskbarDefaultStyle.cpp:16-83`）：

- `default1_text_color/back_color/transparent_color/status_bar_color/specify_each_item_color`
- `default2_*` / `default3_*` …… 一直到 `TASKBAR_DEFAULT_STYLE_NUM`

`Save()` 在 `CTaskbarDefaultStyle::~CTaskbarDefaultStyle` 自动调用（`TaskbarDefaultStyle.cpp:13`）。

### 3.8 `[histroy_traffic]` —— 历史流量统计

- `use_log_scale`、`sunday_first`、`view_type`

### 3.9 `[other]` —— 杂项

- `no_multistart_warning`、`exit_when_start_by_restart_manager`、`debug_log`、`notify_interval`、`taksbar_transparent_color_enable`、`last_light_mode`、`show_mouse_panetrate_tip`、`show_dot_net_notinstalled_tip`

### 3.10 `[app]` —— 元数据

- `version`（写当前 `VERSION`，见 `TrafficMonitor.cpp:442`）

### 3.11 附加 section

- `[plugin_display_str_taskbar_window]` —— 任务栏窗口中"插件项"的显示文本（与内置显示文本分开存，`TrafficMonitor.cpp:214`、`TrafficMonitor.cpp:380`）。主窗口插件项的显示文本也存在，但当前代码注释掉了对应的 `SaveDisplayStr`/`SavePluginDisplayStr`（`TrafficMonitor.cpp:336-337`）——主窗口的插件文本仅在运行时通过 `SkinSettingDataFronSkin` 兜底。
- `skin_<皮肤名>` —— `CSkinManager::Save` 写的"每个皮肤的覆盖项"，前缀常量 `SKIN_SETTINGS_PREFIX = L"skin_"`（`SkinManager.cpp:9`）。内容：`text_color`、`specify_each_item_color`、`font_name/size/style`、内置 `DisplayItem` 显示文本、插件 `ItemId` 显示文本。

## 4. 设置对话框族

整体由 `COptionsDlg`（`OptionsDlg.h:13-46`，`IDD_OPTIONS_DIALOG`）作为带标签页的容器，三个 tab 分别为：

| 对话框 | 资源 ID | 持有数据 | 头文件 |
| --- | --- | --- | --- |
| `CMainWndSettingsDlg` | `IDD_MAIN_WND_SETTINGS_DIALOG` | `MainWndSettingData m_data` | `MainWndSettingsDlg.h:11-84` |
| `CTaskBarSettingsDlg` | `IDD_TASKBAR_SETTINGS_DIALOG` | `TaskBarSettingData m_data` | `TaskBarSettingsDlg.h:11-127` |
| `CGeneralSettingsDlg` | `IDD_GENERAL_SETTINGS_DIALOG` | `GeneralSettingData m_data` | `GeneralSettingsDlg.h:8-109` |

构造：`OptionsDlg.h:26-28` 三个 tab 都是 `CTabDlg` 子对话框。

### 4.1 与 theApp 成员的对应关系

`OptionsDlg::OnOK`（见 `TrafficMonitorDlg.cpp:752` 等）在 OK 时把每个 tab 的 `m_data` 拷回 `theApp.m_main_wnd_data` / `m_taskbar_data` / `m_general_data`，再 `SaveConfig()` 落盘。例如：

- `GeneralSettingsDlg.cpp:797-799` 调 `theApp.SetAutoRunByRegistry(false)` + `SetAutoRunByTaskScheduler(false)` + `SetAutoRun(true, m_data.auto_run_by_task_scheduler)`，写注册表/任务计划。
- `GeneralSettingsDlg.cpp:525-526` 写 `m_data.portable_mode`（实际值会被 `theApp.SaveGlobalConfig()` 持久化到 `global_cfg.ini`，见 §6）。
- `MainWndSettingsDlg.cpp` 通过 `OnBnClickedResotreSkinDefaultButton` 等把改动写回 `m_main_wnd_data`，并在 `TrafficMonitorDlg.cpp:858-861` 把 `ToSkinSettingData()` 推到 `CSkinManager::AddSkinSettingData` ——这是"皮肤的覆盖项"机制的核心入口。

### 4.2 主对话框上挂载点

`CTrafficMonitorDlg` 打开 `COptionsDlg` 的位置在菜单命令 `OnOptions` 等处（`TrafficMonitorDlg.cpp`）；切到哪个 tab 由 `COptionsDlg(int tab = 0, ...)` 第一个参数控制（`OptionsDlg.h:18`）。

### 4.3 颜色二级对话框

- `CMainWndColorDlg`（`MainWndColorDlg.h:9-36`）：用 `CColorSettingListCtrl` 列出 `map<CommonDisplayItem, COLORREF>`，双击行弹出 `CMFCColorDialogEx` 选色；构造参数接收"主窗口颜色表"（`MainWndColorDlg.cpp`）。
- `CTaskbarColorDlg`（`TaskbarColorDlg.h:9-37`）：同模式，但针对 `TaskbarItemColor{label,value}` 两种颜色。
- `CAutoAdaptSettingsDlg`（`CAutoAdaptSettingsDlg.cpp`）：任务栏窗口在 Win10 自动主题时选"深色/浅色"使用哪个预设（`dark_default_style`、`light_default_style`、`auto_save_taskbar_color_settings_to_preset`），通过 `TaskbarItemColor` 写入 `taskbar_default_style_*` 段。

### 4.4 与数据结构的归属关系（小结）

| 对话框 | 绑定的 theApp 成员 | INI section |
| --- | --- | --- |
| `CMainWndSettingsDlg` | `m_main_wnd_data` + 部分 `m_cfg_data`（`m_show_more_info`、`swap_up_down`） | `[config]` |
| `CTaskBarSettingsDlg` | `m_taskbar_data` + 触发 `CTaskbarDefaultStyle::SaveConfig` | `[task_bar]` + `[taskbar_default_style]` |
| `CGeneralSettingsDlg` | `m_general_data` | `[general]` / `[notify_tip]` / `[other]`（部分） / `global_cfg.ini` |
| `CSkinDlg` / `CSkinAutoAdaptSettingDlg` | `m_cfg_data.skin_*` | `[skins]` + `skin_*` |
| `CMainWndColorDlg` | 通过 `m_main_wnd_data.text_colors` | (运行时内存，不持久化到 INI，由 `CSkinManager` 接管) |
| `CTaskbarColorDlg` | 通过 `m_taskbar_data.text_colors` | `[task_bar]`（通过 SaveConfig 整体写） |

注意：`m_main_wnd_data.text_colors/font/disp_str/specify_each_item_color` 在 `LoadConfig` 里**故意被初始化为固定值**（`TrafficMonitor.cpp:123-131`），注释里写明"现在保存到每个皮肤的独立设置中"。真正持久的覆盖项在 `CSkinManager::m_skin_setting_data_map`，由 `MainWndSettingData::ToSkinSettingData/FormSkinSettingData`（`CommonData.cpp:174-190`）在切换皮肤时来回转换。

## 5. 数据结构定位与字段分组

定义全部在 `CommonData.h`，实现分布在 `CommonData.cpp`。

### 5.1 MainConfigData —— `CommonData.h:190-221`

主窗口/任务栏窗口之外的"全局开关 + 杂项"，由 `theApp.m_cfg_data`（`TrafficMonitor.h:75`）持有：

- 窗口位置/可见性：`m_transparency`、`m_show_more_info`、`m_show_task_bar_wnd`、`m_hide_main_window`、`m_position_x/y`
- 网络选择：`m_auto_select`、`m_select_all`、`m_connection_name`
- 皮肤：`m_skin_name`、`skin_auto_adapt`、`skin_name_light_mode/dark_mode`
- 通知区图标：`m_dft_notify_icon`、`m_notify_icon_selected`、`m_notify_icon_auto_adapt`
- 历史流量视图：`m_use_log_scale`、`m_view_type`、`m_sunday_first`
- 插件：`plugin_disabled`（`StringSet`，见 §7）

### 5.2 MainWndSettingData —— `CommonData.h:264-276`

继承自 `PublicSettingData`（`CommonData.h:244-260`）。独有字段：

- `text_colors`、`swap_up_down`、`hide_main_wnd_when_fullscreen`
- `m_always_on_top`、`m_lock_window_pos`、`m_mouse_penetrate`、`m_alow_out_of_border`
- 转换函数 `FormSkinSettingData` / `ToSkinSettingData`（`CommonData.cpp:174-190`）——这是"皮肤覆盖项 ↔ 主窗口运行时数据"互转的接口。

### 5.3 TaskBarSettingData —— `CommonData.h:292-350`

继承自 `PublicSettingData`。最大的一组：

- 颜色：`back_color`、`transparent_color`、`status_bar_color`、`text_colors`（`map<CommonDisplayItem, TaskbarItemColor>`，`TaskbarItemColor` 定义 `CommonData.h:280-289`）、4 个 `dft_*` 默认值
- 透明度判定：`IsTaskbarTransparent()` / `SetTaskabrTransparent()`（`CommonData.cpp:193-` 之后）
- 自动适配：`auto_adapt_light_theme`、`dark_default_style`、`light_default_style`、`auto_set_background_color`、`auto_save_taskbar_color_settings_to_preset`
- 显示项集：`item_order`（`CTaskbarItemOrderHelper`，`TaskbarItemOrderHelper.h`）、`display_item`（`DisplayItemSet`）、`plugin_display_item`（`StringSet`）
- 排列：`show_taskbar_wnd_in_secondary_display`、`secondary_display_index`、`value_right_align`、`digits_number`、`horizontal_arrange`、`show_status_bar`、`tbar_wnd_on_left`、`tbar_wnd_snap`
- 占用图：`cm_graph_type`、`show_graph_dashed_box`、`show_netspeed_figure`、`netspeed_figure_max_value/unit`、`graph_color_following_system`、`GetUsageGraphColor()`（消费主题色，见皮肤文档 §5.2）
- 边距：`item_space`、`vertical_margin`、`window_offset_top/left`，加 4 个 `Valid*` 函数夹紧范围
- Win11：`taskbar_left_space_win11`、`taskbar_right_space_win11`、`avoid_overlap_with_widgets`
- 渲染：`disable_d2d`、`update_layered_window_error_code`、`enable_colorful_emoji`
- 检测：`is_windows_web_experience_detected`

### 5.4 GeneralSettingData —— `CommonData.h:353-401`

"应用级 + 不会因皮肤变化而变"的设置：

- 启动：`check_update_when_start`、`update_source`、`auto_run`、`auto_run_by_task_scheduler`
- 通知：`show_notify_icon`、`traffic_tip_*`、`NotifyTipSettings memory_usage_tip / cpu_temp_tip / gpu_temp_tip / hdd_temp_tip / mainboard_temp_tip`
- 国际化：`language`（`LanguageInfo`）
- 模式/性能：`show_all_interface`、`portable_mode`、`monitor_time_span`
- 硬件：`hard_disk_name`、`cpu_core_name`、`hardware_monitor_item`，及 `IsHardwareEnable/SetHardwareEnable`
- 网络：`connections_hide`（`StringSet`）

### 5.5 PublicSettingData —— `CommonData.h:244-260`

被 `MainWndSettingData` 与 `TaskBarSettingData` 共用的"显示文本/单位/双击动作"字段。

### 5.6 SkinSettingData —— `CommonData.h:232-241`

每个皮肤的覆盖项（字体、显示文本、颜色、是否按项设色），由 `CSkinManager::m_skin_setting_data_map` 持有。

## 6. 全局配置 vs 用户配置：global_cfg.ini 与 LoadGlobalConfig/SaveGlobalConfig

### 6.1 文件位置

`global_cfg.ini` 永远在 exe 同目录（`TrafficMonitor.cpp:467`），与 `config.ini` 可能被存到 AppData 不同。

### 6.2 LoadGlobalConfig

`TrafficMonitor.cpp:464-488`：

1. 拼出 `global_cfg_path = m_module_dir + L"global_cfg.ini"`；
2. 如果 `global_cfg.ini` 不存在，按"AppData 下是否已有 config.ini"判断默认 `portable_mode`（有 → false，无 → true）；
3. 读 `portable_mode`；
4. **试写一次**：`ini.Save()` 用作目录可写测试，结果写入 `m_module_dir_writable`；
5. 如果 exe 在 Temp 目录下 → 强制 `m_module_dir_writable = false`；
6. 如果不可写 → 强制 `m_general_data.portable_mode = false`（最终效果：配置全部走 AppData）。

### 6.3 SaveGlobalConfig

`TrafficMonitor.cpp:490-508`：只写一个 key `[config]/portable_mode`。该函数在 `TrafficMonitorDlg::OnOK` 等关闭点被调（`TrafficMonitorDlg.cpp:864`）。

### 6.4 与 LoadConfig/SaveConfig 的边界

| | 文件 | 位置 | 内容 | 调用点 |
| --- | --- | --- | --- | --- |
| `LoadGlobalConfig` / `SaveGlobalConfig` | `global_cfg.ini` | 永远 exe 同目录 | `portable_mode` | `InitInstance` 早期 / `OnOK` 末 |
| `LoadConfig` / `SaveConfig` | `config.ini` | `m_config_dir`（依赖 `portable_mode` 与 `m_module_dir_writable`） | 全部 UI/插件/皮肤/历史开关 | `InitInstance` 后段 / 关闭点 |
| `LoadLanguageConfig` | `config.ini` | 同上 | 仅 `[general]/language` | `LoadGlobalConfig` 之后、`LoadConfig` 之前 |
| `CSkinManager::Save` | `config.ini` | 同上 | 所有 `skin_*` section | 选项设置 OK 后 |

## 7. 开机自启动：auto_start_helper 与 m_module_path_reg

提供两种方式，通过 `GeneralSettingData::auto_run_by_task_scheduler` 切换：

- `false`：注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`。
- `true`：Windows 任务计划程序 `\TrafficMonitor\Autorun for <username>`。

### 7.1 接口

`TrafficMonitor.cpp:636-739`：

- `SetAutoRun(bool auto_run, bool task_scheduler)`（`TrafficMonitor.cpp:636-648`）：按 `task_scheduler` 分发到两个具体实现。
- `SetAutoRunByRegistry(bool)`（`TrafficMonitor.cpp:692-727`）：
  - 打开 `HKCU\...\Run`，写键名 `APP_NAME`（= `TrafficMonitor`）、值 `m_module_path_reg`（`TrafficMonitor.cpp:706`）——这里用到 `m_module_path_reg`：如果原 exe 路径含空格，必须套上引号才能让 Windows 解析正确路径，否则注册表 Run 只会启动到空格处截断。
  - `auto_run=false` 时先 `QueryStringValue` 检查键是否存在再决定要不要 `DeleteValue`。
  - 写注册表前会先 `SetAutoRunByTaskScheduler(false)` 清除任务计划项（`TrafficMonitor.cpp:704`），反之亦然（`TrafficMonitor.cpp:734`）——保证两种方式互斥。
- `SetAutoRunByTaskScheduler(bool)`（`TrafficMonitor.cpp:728-739`）：委托给 `delete_auto_start_task_for_this_user` 与 `create_auto_start_task_for_this_user`（`auto_start_helper.cpp`）。
- `GetAutoRun(*path, task_scheduler)`（`TrafficMonitor.cpp:650-691`）：读注册表 / 任务计划当前值，回写路径供 UI 比较。

### 7.2 auto_start_helper.cpp 的关键细节

`auto_start_helper.cpp` 全量实现任务计划版本，依赖 `taskschd.lib`（`auto_start_helper.cpp:9`）。

- 任务名：`L"Autorun for " + USERNAME`（`auto_start_helper.cpp:67-68`）。
- 任务位置：`\TrafficMonitor`（不存在则创建，`auto_start_helper.cpp:88-102`）。
- 触发器：`TASK_TRIGGER_LOGON`，延迟 `PT03S`（3 秒），避免 explorer 还没启动完（`auto_start_helper.cpp:155-167`）。
- Action：`TASK_ACTION_EXEC`，路径 `GetModuleFileName(NULL, wszExecutablePath, MAX_PATH)`（`auto_start_helper.cpp:71-72`、`199`）——这里用的是 GetModuleFileName 的当前值，**不依赖** `m_module_path_reg`。
- `delete_auto_start_task_for_this_user`（`auto_start_helper.cpp:264-327`）：通过 `ITaskFolder::DeleteTask` 删除。
- `is_auto_start_task_active_for_this_user(*path)`（`auto_start_helper.cpp:329-410`）：
  - 用 `IRegisteredTask::get_Xml` 拿到 XML，再用 `CSimpleXML::GetNode(L"Command", L"Exec")`（`auto_start_helper.cpp:382-385`）抓取实际启动命令的路径。
  - 与 `GetModuleFileName(NULL, ...)` 比对，避免"任务计划里命令指向另一个目录下的 TrafficMonitor.exe"被误判为已设置自启（`auto_start_helper.cpp:386-390`）。这一比较结果同时返回 `*path`，供 UI 展示。
  - 返回 `true` 当且仅当任务存在 + 启用 + 命令路径匹配。

### 7.3 与 m_module_path_reg 的关系

注册表方式下：

- `SetAutoRunByRegistry(true)` 把 `m_module_path_reg` 写入 `Run` 键（`TrafficMonitor.cpp:706`）。
- `GetAutoRun` 读到的值会拿当前 exe 路径（未加引号）去匹配 ——见 `TrafficMonitor.cpp:683` `return (m_module_path_reg == buff);`：因为读取时 `buff` 是注册表 API 返回的字符串，跟 `m_module_path_reg` 同样在路径含空格时带引号，匹配一致。

任务计划方式下完全不用 `m_module_path_reg`（用 `GetModuleFileName` 当场取）。

## 8. 插件禁用列表：LoadPluginDisabledSettings 的落地

### 8.1 数据载体

`MainConfigData::plugin_disabled`（`CommonData.h:219`）是 `StringSet`，定义在 `CommonData.h:157-169`，本质是 `set<wstring>`，有 `FromString/ToString/FromVector/ToVector/Contains/SetStrContained` 序列化接口。

### 8.2 加载时机

`LoadPluginDisabledSettings`（`TrafficMonitor.cpp:458-462`）在 `InitInstance` 中非常早执行——**早于 `m_plugins.LoadPlugins()`**（`TrafficMonitor.cpp:1015-1017`，顺序：`LoadPluginDisabledSettings` → `m_plugins.LoadPlugins` → `LoadConfig`）。

实现细节：

```cpp
void CTrafficMonitorApp::LoadPluginDisabledSettings()
{
    CIniHelper ini{ m_config_path };
    m_cfg_data.plugin_disabled.FromString(ini.GetString(L"config", L"plugin_disabled", L""));
}
```

—— 用底层 `CIniHelper` 而不是 `CSettingsHelper`，因为它要的是任意 section/key 的字符串。

### 8.3 落地时机

- 加载后写入 `m_cfg_data.plugin_disabled`，字符串序列化存到 INI（`StringSet::ToString` 走自定义协议，详见 `CommonData.cpp` 中 `StringSet` 实现）。
- 保存点：`SaveConfig` 末尾 `ini.WriteString(L"config", L"plugin_disabled", m_cfg_data.plugin_disabled.ToString())`（`TrafficMonitor.cpp:440`）。
- 由谁消费：插件管理器使用此集合在 `LoadPlugins` 时跳过被禁用的插件项；UI 上由 `PluginManagerDlg`（`PluginManagerDlg.cpp`）勾选/取消勾选后通过 `theApp.m_cfg_data.plugin_disabled` 落地。

## 9. 关键文件清单

- `TrafficMonitor/TrafficMonitor.h:34-45` —— 全部路径成员。
- `TrafficMonitor/TrafficMonitor.cpp:915-957` —— `InitInstance` 路径装配。
- `TrafficMonitor/TrafficMonitor.cpp:65-287` / `289-456` —— `LoadConfig` / `SaveConfig`，几乎所有 section 的来源。
- `TrafficMonitor/TrafficMonitor.cpp:458-462` / `464-488` / `490-508` —— 插件禁用列表 + `LoadGlobalConfig` / `SaveGlobalConfig`。
- `TrafficMonitor/TrafficMonitor.cpp:636-739` —— `SetAutoRun*` / `GetAutoRun`。
- `TrafficMonitor/auto_start_helper.cpp` —— 任务计划版本的开机自启实现。
- `TrafficMonitor/IniHelper.h/.cpp` —— 底层 INI 读写。
- `TrafficMonitor/SettingsHelper.h/.cpp` —— 项目专用包装（默认绑 `theApp.m_config_path`）。
- `TrafficMonitor/CommonData.h:190-401` —— `MainConfigData` / `PublicSettingData` / `MainWndSettingData` / `TaskBarSettingData` / `GeneralSettingData` / `SkinSettingData` 定义。
- `TrafficMonitor/OptionsDlg.h/.cpp` —— 带 3 个 tab 的容器。
- `TrafficMonitor/MainWndSettingsDlg.h/.cpp` —— 主窗口设置。
- `TrafficMonitor/TaskBarSettingsDlg.h/.cpp` —— 任务栏窗口设置。
- `TrafficMonitor/GeneralSettingsDlg.h/.cpp` —— 常规设置。
- `TrafficMonitor/MainWndColorDlg.h/.cpp` / `TaskbarColorDlg.h/.cpp` / `CAutoAdaptSettingsDlg.h/.cpp` —— 颜色二级对话框。
- `TrafficMonitor/TaskbarDefaultStyle.cpp:16-83` —— `[taskbar_default_style]` 的读写。
- `TrafficMonitor/SkinManager.cpp:115-147` —— `skin_*` section 的写。