# 03 · 生命周期与关键时序

本文件把 TrafficMonitor 从双击 exe 到退出的全流程拆成 6 个"时序段"，每段标出触发点、关键函数、与主题文档的交叉引用。所有引用都基于源码。

## 3.1 进程启动时序

```
[用户]  双击 TrafficMonitor.exe
   │
   ▼
Win32 入口 → CTrafficMonitorApp::CTrafficMonitorApp 构造
   ├─ self = this                                                      TrafficMonitor.cpp:37
   ├─ CRASHREPORT::StartCrashReport()                                 TrafficMonitor.cpp:49
   ├─ 探测 Win11（m_win_version.IsWindows11OrLater）                   TrafficMonitor.cpp:51
   ├─ CheckWindows11Taskbar()                                         TrafficMonitor.cpp:55
   └─ m_theme_color = CCommon::GetWindowsThemeColor()                 TrafficMonitor.cpp:56
   │
   ▼
CWinApp::InitInstance（CTrafficMonitorApp 重写）
   ├─ LoadLanguageConfig()                                            TrafficMonitor.cpp:59
   ├─ LoadConfig() / LoadPluginDisabledSettings()                      TrafficMonitor.cpp:65
   │     见 topics/settings-and-config.md
   ├─ InitInstance 期间启动 MonitorThread + InitOpenHardwareMonitorLibThread
   │     见 TrafficMonitor.cpp:1133-1138（创建线程）
   │     见 topics/monitor-data-collection.md §驱动模型
   │     见 topics/hardware-monitoring.md §异步初始化
   ├─ m_plugins.LoadPlugins()                                         TrafficMonitor.cpp:1016 之后
   │     见 topics/plugin-system.md §2.2 LoadPlugins 13 步流程
   ├─ InitMenuResourse()  把插件占位插入到主窗口/任务栏右键菜单       TrafficMonitor.cpp:782
   └─ CTrafficMonitorDlg::DoModal（主窗口进入消息循环）
```

`CTrafficMonitorApp` 同时继承 `CWinApp` 和 `ITrafficMonitor`，所以 `OnInitialize(&theApp)` 给插件注入的指针就是主程序本身（`TrafficMonitor.h:31`）。

## 3.2 主窗口创建与初始化

`CTrafficMonitorDlg::OnInitDialog`（`TrafficMonitorDlg.cpp:1076` 起）：

1. `IniConnection()` — 首次构建网卡列表（`TrafficMonitorDlg.cpp:385-485`，详见 topics/monitor-data-collection.md §3）。
2. 依据 `m_win_version` 选择任务栏窗口类型（Classical / Win11 / Wine），见 `OpenTaskBarWnd`（参见 topics/ui-main-dialog.md）。
3. 启动三组定时器：
   - `MONITOR_TIMER`（`m_general_data.monitor_time_span`，默认 1000 ms，范围 200–30000）— 驱动 `MonitorThreadCallback` 工作线程内的 `DoMonitorAcquisition`，参见 topics/monitor-data-collection.md。
   - `MAIN_TIMER`（1000 ms，固定）— 托盘 tip 刷新、菜单刷新、低频维护（`TrafficMonitorDlg.cpp:1113`）。
   - `TASKBAR_TIMER`（100 ms）— 任务栏窗口的插值/动画时序（`TrafficMonitorDlg.cpp:1142`）。
4. 注册通知区图标（`CDllFunctions::Shell_NotifyIconW` 封装），见 topics/ui-main-dialog.md。

## 3.3 数据采集 → UI 重绘 → 插件回调

完整循环见 `topics/monitor-data-collection.md` 与 `skill/overview/02-data-flow.md`。本节突出**时序上的因果链**：

```
[采集线程 MonitorThreadCallback]                      [主线程]
  m_monitor_data_required == true?
     │
     ├─ DoMonitorAcquisition()                      ─→  theApp.m_*  ←┐
     │   ① GetIfTable / GlobalMemoryStatusEx                       │
     │   ② m_cpu_usage_helper.GetCpuUsage()                        │  写入
     │   ③ 温度: m_pMonitor->CpuTemperature() 等                   │
     │   ④ m_today_*_traffic 累计 + 跨天翻页                       │
     │   ⑤ plugins[i]->DataRequired()                             │
     │   ⑥ plugins[i]->OnMonitorInfo(MonitorInfo)                  │
     │   ⑦ SendMessage(WM_MONITOR_INFO_UPDATED) ───────────────→   │
     │                                                              │
     │                                              OnMonitorInfoUpdated()
     │                                                 ├─ 主窗口 Invalidate
     │                                                 ├─ 任务栏窗口刷新
     │                                                 └─ 托盘 tip 刷新
```

关键不变式：

- **写入唯一发生点**：`DoMonitorAcquisition` 内部。其它代码读 `theApp.m_*`，不写。
- **线程同步边界**：所有 UI 重绘都通过 `SendMessage` 回到主线程，因此 `OnPaint` 路径里的读不需要锁。`m_pMonitor` 本身被 `m_minitor_lib_critical` 保护（`TrafficMonitor.cpp:1141-1153`）。
- **插件自取 vs 主程序推**：`DataRequired` 让插件自取；`OnMonitorInfo` 把快照推过去——两条路并存，按"该数据由谁拥有"分流。

## 3.4 用户操作链

### 3.4.1 设置变更

```
用户改 COptionsDlg 某页 → 按"确定"
   └─→ WM_SETTINGS_APPLIED
        └─→ CTrafficMonitorDlg::OnSettingsApplied
              ├─ 把新值写回 theApp.m_main_wnd_data / m_taskbar_data / ...
              ├─ 重建任务栏窗口（如布局/字体/颜色变化）
              ├─ 重载皮肤（m_skin_name 变化时）
              └─ SendSettingsToPlugin()                                    TrafficMonitor.cpp:1241
                    └─ 批量推 EI_*_WND_* flags + EI_LABEL/VALUE_TEXT_COLOR
                         走 OnExtenedInfo，参见 topics/plugin-system.md §6
```

### 3.4.2 右键菜单 → 插件命令

```
CTrafficMonitorDlg::OnRButtonUp                                         TrafficMonitorDlg.cpp:1981
   ├─ CheckClickedItem(point)  锁定被点的 IPluginItem*
   ├─ 如果是插件项 + GetAPIVersion >= 3：OnMouseEvent(MT_RCLICKED) 命中则中止
   ├─ 选菜单：插件项 → m_main_menu_plugin；否则 → 普通菜单
   ├─ 占位改名为插件名 + 挂 GetPluginIcon()
   └─ UpdatePluginMenu(m_main_menu_plugin_sub_menu, plugin, 2)
         重建 GetCommandCount / GetCommandName / GetCommandIcon          TrafficMonitor.cpp:1266

用户点条目 → WM_COMMAND(ID_PLUGIN_COMMAND_START + i)
   └─ CTrafficMonitorDlg::OnCommand → OnPluginCommand(i, hWnd, nullptr) TrafficMonitorDlg.cpp:2190
```

完整调用图见 topics/plugin-system.md §4.1–§4.4。

### 3.4.3 DPI 变化

- 主窗口：`OnDpichanged` 响应 `WM_DPICHANGED`（参见 topics/ui-main-dialog.md）。
- 任务栏窗口：`CheckWindowMonitorDPIAndHandle` 模板在显示器的 DPI 变化时调用用户回调调整布局（`TaskBarDlg.h:60-76`）。

## 3.5 后台线程生命周期

| 线程 | 创建 | 退出 | 关键同步 |
| --- | --- | --- | --- |
| `MonitorThreadCallback`（采集） | `CTrafficMonitorApp::InitInstance` 中 `AfxBeginThread` | `m_is_thread_exit = true` 后线程主循环检测并退出 | `m_monitor_data_required` 触发本帧采集；`SendMessage` 把结果回主线程 |
| `InitOpenHardwareMonitorLibThreadFunc`（硬件库） | `InitInstance` 中 `AfxBeginThread`，仅执行一次 `CreateInstance()` | 一次性，函数返回即结束 | `m_minitor_lib_critical` 保护 `m_pMonitor` |
| 单实例互斥 | `AppAlreadyRuningDlg` 通过 `CreateMutex` | DEBUG/Release 互斥量名不同 | 第二个实例把命令行通过 `::PostMessage` 转发给第一个实例 |

## 3.6 进程退出

`CTrafficMonitorApp` 析构路径（按调用顺序推测，未在源码中显式跟踪）：

1. `CWinApp::ExitInstance` 触发 MFC 标准清理。
2. `~CPluginManager` 析构中遍历 `m_modules` 对每个非 null `plugin_module` 调 `FreeLibrary`（`PluginManager.cpp:23-24`）。
3. `m_pMonitor.reset()` 释放 LibreHardwareMonitor 包装。
4. `SaveConfig()` 持久化 `m_main_wnd_data / m_taskbar_data / m_general_data / m_cfg_data`。

## 3.7 关键时序不变量速查

| 不变量 | 落在何处 | 原因 |
| --- | --- | --- |
| UI 永远只读 `theApp.m_*` | 所有 OnPaint / ShowInfo / UpdateNotifyIconTip | 写入由 SendMessage 收敛到主线程 |
| 插件不应在 `GetItemValueText` 重新采集 | `include/PluginInterface.h:32-36` 注释 + `DataRequired` 已分发 | 该函数被频繁调用 |
| `WITHOUT_TEMPERATURE` 是**编译期**开关 | `TrafficMonitor.vcxproj` 6 套 Lite 配置 | 与运行时 `m_general_data.IsHardwareEnable()` 互补 |
| 设置变更必须重建任务栏窗口 | `OnSettingsApplied` 统一处理 | 布局/字体/颜色都影响测量 |
| 插件 disabled 应在 `LoadLibrary` 之前判断 | 当前实现是 LoadLibrary 之后才检查（`PluginManager.cpp:54-66`）—— 后续重构值得前置 | 减少无用 LoadLibrary 副作用 |

## 3.8 与本节相关的关键文件索引

- 入口 / 配置加载：`TrafficMonitor/TrafficMonitor.cpp`（构造、`InitInstance`、`LoadConfig`、`SaveConfig`）。
- 主对话框：`TrafficMonitor/TrafficMonitorDlg.cpp`（`OnInitDialog`、`OnTimer`、`OnMonitorInfoUpdated`、`OnRButtonUp`、`OnCommand`、`OnSettingsApplied`）。
- 任务栏窗口：`TrafficMonitor/TaskBarDlg.cpp`（`OnRButtonUp`、`OnCommand`、`OnInitMenu`、`OnPaint`/`ShowInfo`）。
- 插件生命周期：`TrafficMonitor/PluginManager.cpp`（`LoadPlugins`、`~CPluginManager`）+ `TrafficMonitor/TrafficMonitor.cpp`（`SendSettingsToPlugin`、`UpdatePluginMenu`、`OnInitialize` 间接路径）。
- 单实例与崩溃：`TrafficMonitor/AppAlreadyRuningDlg.*`、`TrafficMonitor/crashtool.*`。