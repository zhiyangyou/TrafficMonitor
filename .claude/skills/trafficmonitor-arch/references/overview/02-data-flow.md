# 02 · 数据流：从硬件/网卡到屏幕像素

> 范围：单条"原始信号 → UI 像素"的全链路追踪 + 跨窗口数据共享 + 插件回调链。
> 链路以"网速"为代表项；CPU 占用、内存、温度、GPU 走相同的"采集 → 缓存 → 广播 → 绘制"骨架，差异只在采集源（PDH/Iphlpapi/OpenHardwareMonitor）。

## 3.1 总体分层

```
┌───────────────────────────┐
│ ③ UI 消费（绘图）         │ CTrafficMonitorDlg::OnPaint / CTaskBarDlg::OnPaint / ShowInfo
│                           │   ↓ 读
│ ② 跨窗口数据共享（缓存） │ CTrafficMonitorApp 上的公共字段（m_in_speed / m_cpu_usage / ...）
│   ↑                       │
│ ① 采集 + 广播            │ CTrafficMonitorDlg::DoMonitorAcquisition 写 + SendMessage(WM_MONITOR_INFO_UPDATED)
│   ↑                       │
│ 0 原始信号                │ Iphlpapi.GetIfTable / GlobalMemoryStatusEx / PDH 计数器 / OpenHardwareMonitor
└───────────────────────────┘
```

## 3.2 全链路追踪：以"网速"为例

### 3.2.1 0 → 1 原始信号 → 字段
- 来源：`Iphlpapi.dll` 的 `GetIfTable`（`TrafficMonitorDlg.cpp:1171,1178,1184`），返回 `MIB_IFTABLE`。
- 缓存位置：`CTrafficMonitorDlg` 自身的 `MIB_IFTABLE* m_pIfTable` 与 `DWORD m_dwSize`（`TrafficMonitorDlg.h:61-62`），在 `DoMonitorAcquisition` 内用 SEH 包裹调用以处理 `ERROR_INSUFFICIENT_BUFFER`（`TrafficMonitorDlg.cpp:1168-1186`）。
- 取值：从中挑选 `m_connection_selected` 索引对应那条连接（或全部求和，见 `m_cfg_data.m_select_all`）得到本帧的 `m_in_bytes/m_out_bytes`（`TrafficMonitorDlg.cpp:1190-1209`）。
- 与上一帧相减得到 `cur_in_speed/cur_out_speed`，最终换算成字节/秒写入：
  - `theApp.m_in_speed`（`TrafficMonitorDlg.cpp:1241`）
  - `theApp.m_out_speed`（`TrafficMonitorDlg.cpp:1241`）
- 异常保护：`m_in_bytes == m_out_bytes == 0` / 上次为 0 / 连接切换 / 计数器反转 都把速度置 0（`TrafficMonitorDlg.cpp:1214-1221`）。

### 3.2.2 1 → 2 写入共享缓存
`CTrafficMonitorApp` 上的字段是被多个窗口"共享的真相"——见 `TrafficMonitor.h:48-60`：

```
unsigned __int64 m_in_speed{};       // 下载速度（字节/秒）
unsigned __int64 m_out_speed{};      // 上传速度
int m_cpu_usage{ -1 };
int m_memory_usage{ -1 };
int m_used_memory{};
int m_total_memory{};
float m_cpu_temperature{ -1 };
float m_cpu_freq{ -1 };
float m_gpu_temperature{ -1 };
float m_hdd_temperature{ -1 };
float m_main_board_temperature{ -1 };
int m_gpu_usage{ -1 };
int m_hdd_usage{ -1 };
unsigned __int64 m_today_up_traffic{};
unsigned __int64 m_today_down_traffic{};
```

这些字段由 `DoMonitorAcquisition` 在采集线程里写入（`TrafficMonitorDlg.cpp:1241,1354,1405,1410,1411,1438-1494`），主窗口、任务栏窗口、托盘提示、插件回调全部从这里读取。

### 3.2.3 1 → 2 → 3 广播
- `DoMonitorAcquisition` 末尾先调用 `SendMessage(WM_MONITOR_INFO_UPDATED)`（`TrafficMonitorDlg.cpp:1524`）——消息回到主线程。
- 接收：`CTrafficMonitorDlg::OnMonitorInfoUpdated`（在 `TrafficMonitorDlg.h:263` 声明；实现见 cpp 同名函数），它会调用 `Invalidate()/UpdateWindow()` 或直接刷新任务栏窗口，从而触发 `OnPaint`。
- 同一函数末尾还会**同步**遍历所有插件下发本次采集结果：
  ```
  for (plugin_info in m_plugins.GetPlugins()) {
      plugin_info.plugin->DataRequired();                 // 让插件采集自己的数据
      ITMPlugin::MonitorInfo monitor_info = { ... };     // 填入本次硬件/网络快照
      plugin_info.plugin->OnMonitorInfo(monitor_info);   // 下发给插件
  }
  ```
  见 `TrafficMonitorDlg.cpp:1499-1519`。
- **关键约定**：`IPluginItem::GetItemValueText`（`include/PluginInterface.h:36` 的注释）被频繁调用，因此插件不应该在 `GetItemValueText` 内重新采集数据——采集要放在 `DataRequired` 中，结果缓存到插件自己内部。

### 3.2.4 3 UI 消费（绘制）
- **主窗口**：`CTrafficMonitorDlg::OnPaint` 读取 `theApp.m_in_speed/m_out_speed/...` → 通过 `CCommon::DataSizeToString`（`Common.h:50-57`）格式化为字符串 → 用 `CDrawCommon`/`CTaskBarDlgDrawCommon` 绘制到 GDI/D2D DC。皮肤文本/字体/颜色来自 `theApp.m_main_wnd_data` + `CSkinFile`。
- **任务栏窗口**：`CTaskBarDlg::ShowInfo(CDC* pDC)`（`TaskBarDlg.cpp:60` 起）按 `m_taskbar_data.display_item` + `m_taskbar_data.plugin_display_item` 顺序遍历每个 `CommonDisplayItem`：
  - 内置项 → `CTaskBarDlg::DrawDisplayItem(drawer, type, rect, label_width, vertical)`（`TaskBarDlg.h:176`）。
  - 插件项 → `CTaskBarDlg::DrawPluginItem(drawer, IPluginItem*, rect, label_width, vertical)`（`TaskBarDlg.h:184`），内部会调 `pItem->GetItemValueText()`（默认模式）或 `pItem->DrawItem(hDC, ...)`（`IPluginItem::IsCustomDraw() == true` 时，见 `include/PluginInterface.h:53-76`）。
- **托盘提示**：`CTrafficMonitorDlg::UpdateNotifyIconTip`（声明在 `TrafficMonitorDlg.h:172`）把 `theApp.m_in_speed/m_cpu_usage/...` 拼成 `NOTIFYICONDATA::szTip`（`TrafficMonitorDlg.cpp:654-660`）。
- **数值文本格式化**：`CommonDisplayItem::GetItemValueText(bool is_main_window)`（`DisplayItem.h:66`）把 `theApp.m_*` 字段拼成最终字符串，是 UI 读取的"统一出口"。

## 3.3 跨窗口数据共享机制

主窗口和任务栏窗口是两个独立的 MFC 窗体对象，二者**不直接互调**，全部通过 `CTrafficMonitorApp` 上的字段共享状态：

| 数据 | 写入者 | 读取者 |
| --- | --- | --- |
| `m_in_speed/m_out_speed` | `DoMonitorAcquisition`（`TrafficMonitorDlg.cpp:1241`） | 主窗口 OnPaint、任务栏 ShowInfo、托盘 tip、插件 `OnMonitorInfo` |
| `m_cpu_usage` | `m_cpu_usage_helper.GetCpuUsage()`（`TrafficMonitorDlg.cpp:1354`） | 同上 |
| `m_memory_usage/m_used_memory/m_total_memory` | `GlobalMemoryStatusEx`（`TrafficMonitorDlg.cpp:1405,1410,1411`） | 同上 |
| `m_cpu_freq` | `m_cpu_freq_helper.GetCpuFreq()`（`TrafficMonitorDlg.cpp:1438`） | 同上 |
| `m_cpu_temperature/m_gpu_temperature/...` | `theApp.m_pMonitor->CpuTemperature()` 等（`TrafficMonitorDlg.cpp:1444-1494`，守卫在 `#ifndef WITHOUT_TEMPERATURE`） | 同上 |
| `m_today_up_traffic/m_today_down_traffic` | `CHistoryTrafficFile`（`TrafficMonitorDlg.cpp` 内 `LoadHistoryTraffic`/`SaveHistoryTraffic`） | 主窗口 tooltip、任务栏显示 |

由于所有写入都在 `DoMonitorAcquisition` 内部、且通过 `SendMessage(WM_MONITOR_INFO_UPDATED)` 回到主线程才触发 UI 重绘，**不会出现两个线程同时修改 UI 的情况**。`m_pMonitor` 共享指针另外被 `m_minitor_lib_critical` 临界区保护（`TrafficMonitor.cpp:1141-1153` 的 `UpdateOpenHardwareMonitorEnableState`），用于主线程与"硬件监控使能状态"设置线程之间的互斥。

任务栏窗口的"看全局"能力由 `CTaskBarDlg::DPI()` + `theApp` 共同提供，例如：
- 读取 `theApp.m_taskbar_data.back_color` / `text_colors` 来决定如何绘制（`TaskBarDlg.cpp:116-118` 处的 `FillRect(draw_rect, theApp.m_taskbar_data.back_color)`）。
- 读取 `theApp.m_taskbar_data.display_item` 决定哪些项要画（顺序由 `CTaskbarItemOrderHelper` 提供，见 `TaskBarDlg.cpp:142` 的 `m_map_history_data`）。
- 调整自身 DPI 时通过 `theApp.DPIFromRect` 触发（`TaskBarDlg.h:60`）。

`CTaskbarDefaultStyle`（`TaskbarDefaultStyle.h:8-35`）是任务栏窗口独有的"颜色预设"容器，提供 4 套预设方案（`TASKBAR_DEFAULT_STYLE_NUM=4`），由 `CTrafficMonitorApp::m_taskbar_default_style`（`TrafficMonitor.h:88`）持有；其 `LoadConfig/SaveConfig/ApplyDefaultStyle/ModifyDefaultStyle` 4 个方法与 `TaskBarSettingData` 协作，被 `COptionsDlg` 触发。

## 3.4 采集/广播/绘制的"管道骨架"

定时器驱动的核心循环（`TrafficMonitorDlg.cpp:1567-1580` + `1527-1554` + `1164-1525`）：

```
[主线程]                              [采集线程 MonitorThreadCallback]
  SetTimer(MONITOR_TIMER,             while (true) {
    m_general_data.monitor_time_span)    if (m_monitor_data_required) {
                                          DoMonitorAcquisition();   // 写 theApp.m_*
  OnTimer(MONITOR_TIMER) {              }
    m_monitor_data_required = true;     if (m_is_thread_exit) ...
  }                                     Sleep(10);
  }                                  }

  OnMonitorInfoUpdated() {  ←—— SendMessage(WM_MONITOR_INFO_UPDATED) from DoMonitorAcquisition
    // 触发主窗口和任务栏窗口的 Invalidate / UpdateWindow
    // ShowInfo 在 OnPaint 路径里执行
  }
```

`MAIN_TIMER`（1000 ms，`TrafficMonitorDlg.cpp:1113`）负责托盘 tip 刷新、托盘菜单更新、连不上网时禁用某些项等"低频维护"任务；真正的硬件采集使用可配置的 `MONITOR_TIMER`，默认 1000 ms（`m_general_data.monitor_time_span`，见 `TrafficMonitor.cpp:76-78`，范围 200–30000 ms）。

`TASKBAR_TIMER`（100 ms，`TrafficMonitorDlg.cpp:1142`）驱动 `m_taskbar_timer_cnt`，仅用于任务栏窗口内部的插值/动画时序。

## 3.5 插件回调链

### 3.5.1 加载期（一次性）

```
CTrafficMonitorApp::InitInstance
  → CTrafficMonitorApp::LoadPluginDisabledSettings（TrafficMonitor.cpp:1016）
  → m_plugins.LoadPlugins()  （PluginManager.cpp:35-135）
       遍历 plugins\*.dll
         LoadLibrary
         ReplacePluginDrawTextFunction(module)        // 改 IAT, 把 User32!DrawText* 钩进来
         GetProcAddress("TMPluginGetInstance")
         plugin->GetAPIVersion() > 0 ?
           plugin->OnExtenedInfo(EI_CONFIG_DIR, ...)  // 下发插件配置目录
           plugin->OnInitialize(&theApp)              // API v7+, 注入 ITrafficMonitor*
           plugin->GetInfo(...) x TMI_MAX
           while ((item = plugin->GetItem(i++)) != null) m_plugins.push_back(item)
  → ReplaceMfcDrawTextFunction()                      // 把 MFC 自己的 DrawText 也钩进来
```

- `ITrafficMonitor*` 通过 `OnInitialize` 注入后，插件可以随时调用主程序的方法（`GetMonitorValue`、`ShowNotifyMessage` 等，接口见 `include/PluginInterface.h:329-423`）。
- `DrawText` 系列钩子用于解决 GDI 文本与 D2D 共存时的对齐/字体回退问题：插件和 MFC 的 `DrawTextW` 调用会被改写到主程序的 `User32DrawTextManager` 实现，桥接到当前 D2D/DirectWrite 上下文（`DrawTextManager.h`）。

### 3.5.2 运行期（每次采集）

```
DoMonitorAcquisition 末尾（TrafficMonitorDlg.cpp:1499-1519）
  for each (plugin_info in theApp.m_plugins.GetPlugins()) {
      if (plugin_info.plugin != null) {
          plugin_info.plugin->DataRequired();         // 插件自己采集（重要：GetItemValueText 内不要再采集）
          ITMPlugin::MonitorInfo mi;
          mi.up_speed = theApp.m_out_speed;            // 注意字段名 up/down
          mi.down_speed = theApp.m_in_speed;
          mi.cpu_usage = theApp.m_cpu_usage;
          mi.memory_usage = theApp.m_memory_usage;
          mi.gpu_usage = theApp.m_gpu_usage;
          mi.hdd_usage = theApp.m_hdd_usage;
          mi.cpu_temperature = theApp.m_cpu_temperature;
          mi.gpu_temperature = theApp.m_gpu_temperature;
          mi.hdd_temperature = theApp.m_hdd_temperature;
          mi.main_board_temperature = theApp.m_main_board_temperature;
          mi.cpu_freq = theApp.m_cpu_freq;
          plugin_info.plugin->OnMonitorInfo(mi);
      }
  }
```

注意 `OnMonitorInfo` 在 `ITMPlugin` 文档里已被标记为"将弃用"，`ITrafficMonitor::GetMonitorValue` 是新版接口（`include/PluginInterface.h:362-369` 注释 "（ITMPlugin::OnMonitorInfo 将被弃用）"）。

### 3.5.3 绘制期（每次重绘）

主程序永远通过 `IPluginItem` 接口拿到要显示的字符串/绘制回调，分两种模式：

- **默认模式**（`IPluginItem::IsCustomDraw() == false`，默认实现见 `include/PluginInterface.h:53`）：
  - 主程序在 `CTaskBarDlg::DrawPluginItem` 中调 `item->GetItemLableText()` + `item->GetItemValueText()`（缓存的副本），用当前 `drawer`（`IDrawCommon`）的 `DrawWindowText` 画到任务栏窗口。
  - `item->GetItemValueSampleText()` 决定布局宽度（`include/PluginInterface.h:42-44` 的注释）。
- **自定义绘制**（`IsCustomDraw() == true`）：
  - 主程序准备 `HDC x, y, w, h, dark_mode` 后调 `item->DrawItem(hDC, x, y, w, h, dark_mode)`（`include/PluginInterface.h:67-76`）。
  - `item->GetItemWidth()` 返回 96 DPI 下的最小宽度（主程序按当前 DPI 放大）。
  - `item->GetItemWidthEx(hDC)` 是更高精度、可读取 hDC 的宽度获取方式（API v3+）。

### 3.5.4 菜单/命令/通知

- 启动后 `CTrafficMonitorApp::InitMenuResourse`（`TrafficMonitor.cpp:782-869`）把插件的子菜单占位插入到主窗口/任务栏右键菜单中。
- 用户右键点击插件区域时 `CTrafficMonitorApp::UpdatePluginMenu`（`TrafficMonitor.cpp:1266-1296`）清空占位，调用 `ITMPlugin::GetCommandCount` / `GetCommandName` / `GetCommandIcon` 重新生成命令菜单项。
- 用户触发命令后主程序调 `ITMPlugin::OnPluginCommand(int idx, void* hWnd, void* para)`（`include/PluginInterface.h:303-309`）。
- 插件要显示通知时调 `ITrafficMonitor::ShowNotifyMessage(msg)`（`include/PluginInterface.h:380-383`）→ 落到 `CTrafficMonitorApp::ShowNotifyMessage`（`TrafficMonitor.cpp:1454-1461`）→ `CTrafficMonitorDlg::ShowNotifyTip`（声明在 `TrafficMonitorDlg.h:170`）。
- 工具提示合并：`CTrafficMonitorApp::GetPlauginTooltipInfo`（`TrafficMonitor.cpp:1208-1225`）轮询所有插件的 `ITMPlugin::GetTooltipInfo()`（API v2+）拼到主窗口鼠标提示的尾部。

### 3.5.5 设置变更

- 用户在 `COptionsDlg` 改设置后发出 `WM_SETTINGS_APPLIED`（`TrafficMonitor.cpp:1071` 附近间接使用，消息处理见 `TrafficMonitorDlg.cpp` 内 `OnSettingsApplied`）。
- 处理函数（声明在 `TrafficMonitorDlg.h:271`）会把新设置应用到 `theApp.m_main_wnd_data/m_taskbar_data`、重建任务栏窗口、按需重载皮肤、然后调 `CTrafficMonitorApp::SendSettingsToPlugin`（`TrafficMonitor.cpp:1241-1264`）把新参数通过 `OnExtenedInfo` 推回每个插件。

## 3.6 重要不变量与边界

- **UI 线程安全**：所有读 `theApp.m_*` 的代码（`OnPaint`、`ShowInfo`、`UpdateNotifyIconTip`）都在主线程；所有写都在采集线程内。`SendMessage(WM_MONITOR_INFO_UPDATED)` 把"写后通知"约束在主线程→主线程的同步链上，避免竞态。
- **温度数据的双重开关**：`WITHOUT_TEMPERATURE` 宏（Lite 配置）+ `m_general_data.IsHardwareEnable(HI_CPU/HI_GPU/HI_HDD/HI_MBD)`（`CommonData.h:51-57`）位掩码共同决定某项是否被采集、是否显示。两者分别在编译期和运行期生效。
- **插件禁用列表**：`MainConfigData::plugin_disabled`（`CommonData.h:219`）是文件名集合；`CPluginManager::LoadPlugins`（`PluginManager.cpp:54-58`）在 `LoadLibrary` **之前**就检查 disabled —— 命中直接 `continue`、置 `PS_DISABLE`，避免触发 dll 加载与 IAT hook。
- **OpenHardwareMonitor 异步初始化**：`CTrafficMonitorApp::InitOpenHardwareLibInThread`（`TrafficMonitor.cpp:1133-1138`）在 `InitInstance` 中启动后台线程；后台线程 `InitOpenHardwareMonitorLibThreadFunc`（`TrafficMonitor.cpp:621-634`）中 `theApp.m_pMonitor = OpenHardwareMonitorApi::CreateInstance()`，失败时 `AfxMessageBox(OpenHardwareMonitorApi::GetErrorMessage())`。前端 `theApp.m_pMonitor == nullptr` 也会被 `DoMonitorAcquisition` 中 `#ifndef WITHOUT_TEMPERATURE` 守卫的代码检查（`TrafficMonitorDlg.cpp:1440-1497` 大量 `if (theApp.m_pMonitor != nullptr)`）。
- **DPI 处理**：每个窗口有自己的 DPI 上下文。`CTaskBarDlg` 通过 `theApp.DPIFromRect`（`TaskBarDlg.h:60-76` 的 `CheckWindowMonitorDPIAndHandle` 模板）周期检查任务栏所在显示器的 DPI；变化时调用用户回调调整布局。`CTrafficMonitorDlg` 通过 `OnDpichanged`（`TrafficMonitorDlg.h:261`）响应系统 DPI 变化。

## 3.7 与本节相关的关键文件索引

- 共享数据字段定义：`TrafficMonitor/TrafficMonitor.h:48-104`。
- 采集 + 广播 + 插件下发：`TrafficMonitor/TrafficMonitorDlg.cpp:1164-1525`。
- 任务栏渲染入口：`TrafficMonitor/TaskBarDlg.cpp:60-90`（`ShowInfo`），`TaskBarDlg.h:176-184`（`DrawDisplayItem` / `DrawPluginItem`）。
- 插件接口：`include/PluginInterface.h`。
- 插件管理：`TrafficMonitor/PluginManager.cpp:35-135`。
- 钩子改写：`TrafficMonitor/PluginManager.cpp:410-474`、`TrafficMonitor/DrawTextManager.h`。
- 设置下发：`TrafficMonitor/TrafficMonitor.cpp:1241-1264`（`SendSettingsToPlugin`）。
- 主程序接口实现：`TrafficMonitor/TrafficMonitor.cpp:1390-1497`。
- 跨窗口数据流具体消费者：`TrafficMonitor/TrafficMonitorDlg.cpp:133-165`（`GetMouseTipsInfo`）、`TrafficMonitor/TrafficMonitorDlg.cpp:654-660`（`UpdateNotifyIconTip`）。
