# 04 · 术语表（Glossary）

本文件是 TrafficMonitor 的统一术语表。代码中同名不同义的词、不同名同义的词，都在这里登记。下游所有主题文档都引用本表。

> 写文档时碰到含糊术语，先来这里查；若仍无定义，写入"待澄清"区，等 grill 追问阶段确认。

## A. 进程级对象

| 术语 | 定义 | 出现位置 |
| --- | --- | --- |
| **CTrafficMonitorApp** | 主程序单例类。继承 `CWinApp`（MFC 入口）和 `ITrafficMonitor`（插件反向接口），全局唯一实例 `theApp`。 | `TrafficMonitor.h:31`、`.cpp:37` |
| **theApp** | `CTrafficMonitorApp::self`，全局指针别名，源码中读/写 `theApp.m_*`。 | `TrafficMonitor.cpp:37` |
| **CTrafficMonitorDlg** | 主悬浮窗窗口类（任务栏之外、屏幕悬浮的那一个）。 | `TrafficMonitorDlg.h`、`.cpp` |
| **CTaskBarDlg** | 嵌入任务栏的窗口基类。三个派生：Classical / Win11 / Wine。 | `TaskBarDlg.h`、`.cpp` |
| **CPluginManager** | 插件注册与生命周期管理类。 | `TrafficMonitor/PluginManager.h`、`.cpp` |
| **CSkinManager** | 皮肤加载/枚举/规范化类（按文件名匹配）。 | `TrafficMonitor/SkinManager.h`、`.cpp` |
| **CStrTable** | 多语言字符串表，按 BCP-47 加载 `language/*.ini`。 | `TrafficMonitor/StrTable.h`、`.cpp` |
| **CUpdateHelper** | 主程序更新源（GitHub / Gitee 二选一）。 | `TrafficMonitor/UpdateHelper.h`、`.cpp` |
| **CPluginUpdateHelper** | 插件版本号远端拉取（不下载，只查版本）。 | `TrafficMonitor/PluginUpdateHelper.h`、`.cpp` |
| **CHistoryTrafficFile** | 历史流量持久化文件，按日聚合。 | `TrafficMonitor/HistoryTrafficFile.h`、`.cpp` |
| **CTaskbarDefaultStyle** | 任务栏窗口的"颜色预设"容器，4 套默认样式。 | `TrafficMonitor/TaskbarDefaultStyle.h` |
| **COpenHardwareMonitor / MonitorGlobal** | OpenHardwareMonitorApi 项目里的硬件监控实现与全局实例。 | `OpenHardwareMonitorApi/OpenHardwareMonitorImp.h/.cpp` |
| **PluginInterface.h** | 插件协议头：`IPluginItem` / `ITMPlugin` / `ITrafficMonitor` 三个接口。 | `include/PluginInterface.h` |

## B. 数据共享字段（在 theApp 上）

| 字段 | 含义 | 来源 |
| --- | --- | --- |
| `m_in_speed` / `m_out_speed` | 下载/上传字节每秒 | `TrafficMonitor.h:48-49`，写于 `DoMonitorAcquisition` |
| `m_cpu_usage` | CPU 利用率（0-100），-1 表示无效 | `TrafficMonitor.h:50` |
| `m_memory_usage` | 内存占用百分比 | `TrafficMonitor.h:51` |
| `m_used_memory` / `m_total_memory` | 内存 KB 数 | `TrafficMonitor.h:52-53` |
| `m_cpu_temperature` 等 | 各温度，单位摄氏度，-1 无效 | `TrafficMonitor.h:54-58` |
| `m_cpu_freq` | CPU 频率，GHz，-1 无效 | `TrafficMonitor.h:55` |
| `m_gpu_usage` / `m_hdd_usage` | 利用率 0-100 | `TrafficMonitor.h:59-60` |
| `m_today_up_traffic` / `m_today_down_traffic` | 今日累计字节 | `TrafficMonitor.h:62-63` |

约定：所有"采集失败"都用 **-1** 哨兵表示；UI 层用 `TemperatureToString` / `UsageToString` / `FreqToString` 把 -1 渲染成 `--`（参见 topics/monitor-data-collection.md）。

## C. 设置数据结构

| 结构 | 所在头 | 典型字段 |
| --- | --- | --- |
| `MainConfigData` | `CommonData.h` | `m_transparency`、`m_show_more_info`、`m_show_task_bar_wnd`、`m_position_x/y`、`m_auto_select`、`m_select_all`、`m_connection_name`、`m_skin_name`、`skin_auto_adapt`、`plugin_disabled` |
| `MainWndSettingData` | `CommonData.h` | `m_always_on_top`、`m_lock_window_pos`、`m_mouse_penetrate`、`swap_up_down`、`disp_str`（文本/单位模板） |
| `TaskBarSettingData` | `CommonData.h` | `display_item`、`plugin_display_item`、`back_color`、`text_colors`、`value_right_align`、`net_speed_width`、`unit_byte`、`unit_select`、`separate_with_space` |
| `GeneralSettingData` | `CommonData.h` | `check_update_when_start`、`update_source`、`monitor_time_span`、`hard_disk_name`、`cpu_core_name`、`hardware_monitor_item`、`connections_hide`、`language`、`show_all_interface` |
| `SkinSettingData` | `CommonData.h` | 皮肤布局字段，详见 topics/skin-system.md |

## D. 显示项 / 菜单 / 命令

| 术语 | 定义 |
| --- | --- |
| **DisplayItem / CommonDisplayItem** | 主窗口/任务栏显示的"一行内容"的枚举项（CPU/内存/上传/下载/温度/插件…），见 `CommonData.h`。 |
| **IPluginItem** | 一个插件显示项（对应一个枚举槽位）。 |
| **Plugin Command** | 插件自定义的命令菜单项，由 `GetCommandCount/GetCommandName/GetCommandIcon` 暴露，由 `OnPluginCommand` 接收。命令 ID 走 `ID_PLUGIN_COMMAND_START..MAX` 区间。 |
| **`ID_PLUGIN_OPTIONS` / `ID_PLUGIN_DETAIL`** | 固定的插件菜单项："选项"和"详情"，主程序内置入口。 |
| **`ExtendedInfoIndex`** | 枚举名（`EI_LABEL_TEXT_COLOR`、`EI_*_WND_*` 等共 18 项），主程序→插件单向推送用。 |
| **`EI_DRAW_TASKBAR_WND`** | `OnExtenedInfo` 推送的当前绘制上下文：`L"1"`=任务栏窗口、`L"0"`=皮肤。 |

## E. 渲染抽象

| 术语 | 定义 |
| --- | --- |
| **IDrawCommon** | 绘制抽象接口（详见 `IDrawCommon.h`、`topics/render-api.md`）。 |
| **CDrawCommon** | GDI 实现。 |
| **CDrawCommonEx** | GDI+ 实现。 |
| **CTaskBarDlgDrawCommon** | 任务栏场景专用（支持 DComp / D2D / DXGI）。 |
| **Drawer 后端** | GDI / GDI+ / D2D1 / D3D10.1 / DComp / DXGI 1.2。 |
| **User32DrawTextManager** | 通过 IAT hook 把插件 / MFC 的 `User32!DrawText*` 重定向到主程序上下文的桥梁（解决 GDI/D2D 文本共存）。 |
| **CLazyConstructable** | 延迟构造的 helper 模板，避免在不支持 D2D 的环境下拉起 D2D 依赖。 |

## F. 硬件 / 系统能力

| 术语 | 定义 |
| --- | --- |
| **WITHOUT_TEMPERATURE** | 编译期宏；Lite 版用，标准版不用。 |
| **PDH** | Windows Performance Data Helper。CPU 利用率 / 频率 / GPU / HDD 利用率都走它（见 `TrafficMonitor/PdhHardwareQuery/`）。 |
| **GetSystemTimes** | 备选 CPU 利用率算法（PDH 失败时 fallback）。 |
| **MIB_IFTABLE** | IPHLPAPI 网卡累计字节数表。 |
| **`m_general_data.IsHardwareEnable(HI_*)`** | 运行期温度项开关（位掩码 `HI_CPU/HI_GPU/HI_HDD/HI_MBD`）。 |
| **`-1` 哨兵** | "本帧没有有效数据"的统一表示，UI 渲染为 `--`。 |

## G. 网络/连接配置

| 术语 | 定义 |
| --- | --- |
| **`show_all_interface`** | 是否列出所有网卡（含无 IP 的）。 |
| **`connections_hide`** | 用户指定要从列表里排除的网卡描述符集合。 |
| **`m_auto_select`** | 选流量最大且状态为 OPERATIONAL 的网卡作为当前。 |
| **`m_select_all`** | 求和所有网卡的总流量。`m_auto_select` 与 `m_select_all` 互斥。 |
| **`m_connection_name`** | 用户上次选定的网卡描述符，重启后还原。 |
| **`m_zero_speed_cnt`** | 自动模式下连续零流量的计数，达 30 秒触发重选。 |

## H. 皮肤系统

| 术语 | 定义 |
| --- | --- |
| **`m_skin_name`** | 当前皮肤文件名（不含 `.\\skins\\` 前缀）。 |
| **`m_skin_name_dark_mode` / `m_skin_name_light_mode`** | 暗/亮主题各自独立的皮肤名。 |
| **`skin_auto_adapt`** | 是否启用"暗/亮主题自动切换皮肤"。 |
| **`m_theme_color`** | `CCommon::GetWindowsThemeColor()` 拿到的 Windows 强调色 COLORREF。 |
| **`SkinNameNormalize`** | 文件名规范化（去 `.\\skins\\` 前缀、lowercase 等）。 |

## I. 插件系统

| 术语 | 定义 |
| --- | --- |
| **PluginState** | 插件状态枚举：`PS_SUCCEED` / `PS_MUDULE_LOAD_FAILED` / `PS_FUNCTION_GET_FAILED` / `PS_VERSION_NOT_SUPPORT` / `PS_DISABLE`（注意源码里 `MUDULE` 拼写保留）。 |
| **`TMPluginGetInstance`** | 插件 DLL 必须导出的工厂函数，返回 `ITMPlugin*`。 |
| **`IPluginItem`** | 插件中"一个显示项"接口。 |
| **`ITMPlugin`** | 插件主体接口，`GetAPIVersion` 返回 `7`。 |
| **`ITrafficMonitor`** | 主程序反向暴露给插件的接口；插件通过 `OnInitialize` 拿到 `&theApp`。 |
| **`MonitorInfo`** | 主程序推给插件的快照结构体（11 个字段：up/down/cpu/mem/gpu/hdd + 4 个温度 + cpu_freq）。 |
| **`DataRequired`** | 每帧调用，插件应在此采集并缓存。 |
| **`IsCustomDraw`** | 插件是否自己绘制。`true` 时 `DrawItem` 被调用，文本 getter 被忽略。 |
| **`GetItemWidth` / `GetItemWidthEx`** | 96 DPI 最小宽度 / 基于 hDC 的实测宽度。 |
| **DrawText IAT Hook** | `ReplacePluginDrawTextFunction` + `ReplaceMfcDrawTextFunction` 把 `User32!DrawText*` 改写到 `User32DrawTextManager`。 |
| **`LoadPlugins`** | 启动时扫描 `plugins/*.dll` 一次性加载全部。 |

## J. 配置 / 持久化

| 术语 | 定义 |
| --- | --- |
| **`m_config_path`** | 主 INI 路径，通常 `<exe_dir>\config.ini`，不可写时降级 `%APPDATA%\TrafficMonitor\config.ini`。 |
| **`m_appdata_dir`** | `%APPDATA%\TrafficMonitor` 根目录。 |
| **`m_config_dir`** | 当前生效的配置目录。 |
| **`m_skin_path`** | `<config_dir>\skins`。 |
| **`global_cfg.ini`** | 全局配置（与机器相关，写在 exe 目录）。 |
| **CIniHelper** | 通用 INI 读写封装。 |
| **CSettingsHelper** | 在 CIniHelper 之上做"section 分组 + 类型转换"的扩展封装。 |

## K. 自启动 / 更新 / 崩溃

| 术语 | 定义 |
| --- | --- |
| **`m_module_path_reg`** | 写注册表用的 exe 路径（带引号，处理路径中的空格）。 |
| **Auto-start 双路** | 注册表 `HKCU\...\Run` 与任务计划程序 `\\TrafficMonitor` 二选一，先注册的会把另一个清掉。 |
| **`m_general_data.update_source`** | `0`=GitHub / `1`=Gitee，主程序和插件共用此设置。 |
| **`m_pMonitor`** | `std::shared_ptr<IOpenHardwareMonitor>`，硬件监控接入指针。 |
| **`m_minitor_lib_critical`** | 保护 `m_pMonitor` 读写的 `CCriticalSection`（拼写保留）。 |
| **单实例互斥量** | DEBUG / Release 用不同名字；第二个实例通过 `::PostMessage` 把参数转给已运行实例。 |

## L. 待澄清 / 待 grill 阶段确认

> 写入这里的是"含义有歧义"或"多处不一致"的术语，待 grill 追问阶段统一。

- （暂无）

---

## 与本节相关的关键文件索引

- 全局对象一览：`TrafficMonitor/TrafficMonitor.h`。
- 设置结构：`TrafficMonitor/CommonData.h`。
- 插件协议：`include/PluginInterface.h`。
- 硬件接入：`include/OpenHardwareMonitor/OpenHardwareMonitorApi.h`、`OpenHardwareMonitorApi/OpenHardwareMonitorImp.h`。
- 绘制抽象：`TrafficMonitor/IDrawCommon.h`、`TrafficMonitor/SupportedRenderEnums.h`。