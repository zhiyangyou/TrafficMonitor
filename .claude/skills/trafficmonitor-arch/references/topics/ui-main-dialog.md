# 主悬浮窗（CTrafficMonitorDlg）主题文档

## 1. CTrafficMonitorDlg 概览

`CTrafficMonitorDlg` 继承自 `CDialog`，是 TrafficMonitor 的主悬浮窗口类（`IDD = IDD_TRAFFICMONITOR_DIALOG`），声明于 `TrafficMonitor/TrafficMonitorDlg.h:30`，实现于同名 .cpp。

- 它既负责绘制主悬浮窗自身（皮肤背景、各项目文本），也负责对任务栏子窗口、托盘图标、监控线程等子系统的总调度。
- `CTrafficMonitorDlg::Instance()` 静态方法从 `theApp.m_pMainWnd` 强制转型获取当前实例（`TrafficMonitorDlg.cpp:61`）。
- 通过 `m_tBarDlg`（`CTaskBarDlg*`）持有并管理任务栏窗口的生命周期（`TrafficMonitorDlg.h:58`）。

### 关键方法

| 方法 | 文件位置 | 作用 |
|------|----------|------|
| `DoDataExchange` | `TrafficMonitorDlg.cpp:66` | 空实现，DDX/DDV 入口 |
| `OnInitDialog` | `TrafficMonitorDlg.cpp:1035` | 初始化皮肤、屏幕、连接、托盘图标、字体、工具提示、监控线程与定时器 |
| `OnTimer` | `TrafficMonitorDlg.cpp:1567` | 1 秒级 `MAIN_TIMER`（置顶/穿透/全屏检测/嵌入任务栏重试/通知阈值）和 `MONITOR_TIMER`（设置 `m_monitor_data_required` 触发后台采集线程） |
| `OnPaint` | `TrafficMonitorDlg.cpp:2768` | 委托 `m_skin.DrawInfo(&dc, theApp.m_cfg_data.m_show_more_info)` 完成主窗口绘制 |
| `OnLButtonDblClk` | `TrafficMonitorDlg.cpp:2611` | 先调用 `CheckClickedItem` 决定当前点击项，分发到 `double_click_action`（`DoubleClickAction` 枚举）对应动作 |
| `OnLButtonDown` / `OnLButtonUp` | 头文件 `TrafficMonitorDlg.h:215/274` | `OnLButtonDown` 用于启动拖动；`OnLButtonUp` 当前未做实际点击处理 |
| `OnRButtonUp` | 头文件 `TrafficMonitorDlg.h:214` | 弹出 `theApp.m_main_menu`（主窗口右键菜单） |
| `OnMove` | 头文件 `TrafficMonitorDlg.h:227` | 触发位置变化的保存路径（在 `OnTimer` 中每 30 秒检查 `m_position_x/y` 变化并落盘） |
| `OnExitSizeMove` | `TrafficMonitorDlg.cpp:2858` | 拖动结束后调用 `CheckWindowPos` 校准 |
| `OnDpichanged` | `TrafficMonitorDlg.cpp:2777` | 延迟 500ms 重新读取 DPI，重新加载皮肤布局、字体并刷新 |
| `OnDisplaychange` | `TrafficMonitorDlg.cpp:2850` | 重新计算多屏工作区并 `CheckWindowPos(true)` |
| `OnPowerBroadcast` / `OnColorizationColorChanged` | 头文件 `TrafficMonitorDlg.h:283/284` | 电源事件与 DWM 主题色变化，触发 `GetThemeColor` 等更新 |

### OnInitDialog 的初始化序列（TrafficMonitorDlg.cpp:1035）

1. `SetIcon` 大小图标；`SetWindowText(APP_NAME)`；`ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW)` 隐藏任务栏图标。
2. `theApp.DPIFromWindow(this)`、`GetScreenSize()` 多屏枚举（`Monitors::MonitorEnum`，`TrafficMonitorDlg.h:79`）。
3. `theApp.InitMenuResourse()`、`CSkinManager::Instance().Init()`、`LoadSkinLayout()`（`TrafficMonitorDlg.cpp:879`）从 `m_skin_selected` 加载皮肤。
4. `SetTransparency()`（`TrafficMonitorDlg.cpp:228/233`）应用 `m_cfg_data.m_transparency`，PNG 皮肤走 `m_skin.SetAlpha`、BMP 皮肤走 `SetLayeredWindowAttributes(..., LWA_ALPHA)`。
5. `IniConnection()`（`TrafficMonitorDlg.cpp:385`）枚举网卡。
6. 载入 6 个托盘图标句柄到 `theApp.m_notify_icons[0..5]`（`TrafficMonitorDlg.cpp:1083-1088`），构造 `m_ntIcon`（`NOTIFYICONDATA`），通过 `::Shell_NotifyIcon(NIM_ADD, ...)` 注册托盘（`TrafficMonitorDlg.cpp:1107`）。
7. `LoadHistoryTraffic()`、`SetTimer(MAIN_TIMER, 1000, NULL)`、`SetTimer(MONITOR_TIMER, monitor_time_span, NULL)`、`AfxBeginThread(MonitorThreadCallback, ...)`。
8. `SetItemPosition()`（`TrafficMonitorDlg.cpp:867`）根据 `m_show_more_info` 选用 `layout_l` / `layout_s` 调整窗口尺寸。
9. `SetWindowPos` 恢复上次 `m_position_x/y`；`CheckWindowPos` 校准；`LoadBackGroundImage()`（`TrafficMonitorDlg.cpp:887`）按掩码图 `GetRegionFromImage` 设定 `SetWindowRgn`。
10. `SetTextFont()`（`TrafficMonitorDlg.cpp:930`）将 `m_main_wnd_data` 写入 `m_skin.SetSettingData`。
11. `m_tool_tips.Create(this, TTS_ALWAYSTIP)` 创建工具提示。
12. 如果 `m_hide_main_window` 或位置是 `(0,0)`，调用 `SetTransparency(0)` 临时隐藏。
13. `SetTimer(TASKBAR_TIMER, 100, NULL)` 启动 100ms 任务栏窗口位置/尺寸 tick（与 `m_tBarDlg->OnTimer` 协同）。

## 2. 主窗口与任务栏子窗口的职责边界

| 关注点 | 主窗口（CTrafficMonitorDlg） | 任务栏窗口（CTaskBarDlg / 派生类） |
|--------|----------------------------|------------------------------------|
| 绘制内容 | 皮肤背景图、皮肤布局定义的多项目（`CSkinFile::Layout` 决定位置） | 自行按 `TaskBarSettingData` 排布显示项（`CTaskBarDlg::ShowInfo`） |
| 像素格式 | BMP 皮肤直接绘制；PNG 皮肤走 GDI+ alpha 混合或 `CDrawCommon`/`CDrawCommonEx` | 优先 D2D1/DComp，不可用时降级为 GDI（详见 `ui-taskbar-dialog.md`） |
| 数据来源 | `theApp` 上的速度/温度等公共字段 | 同上 |
| 鼠标交互 | 拖动、双击分发到 `double_click_action`、右键弹出主菜单 | 自身右键菜单 `m_taskbar_menu`，双击分发到 `taskbar_data.double_click_action` |
| 字体设置 | `theApp.m_main_wnd_data.font` | `theApp.m_taskbar_data.font` |
| 显示文本 | `theApp.m_main_wnd_data.disp_str` 通过 `CSkinFile::DrawItemsInfo` 拼装 | `theApp.m_taskbar_data.disp_str` 通过 `CTaskBarDlg::DrawDisplayItem` 拼装 |
| 嵌入任务栏 | 不嵌入 | 通过 `SetParent(GetParentHwnd())` 嵌入二级容器（`CClassicalTaskbarDlg::InitTaskbarWnd`） |
| 透明/穿透 | `SetTransparency` + `SetMousePenetrate`（`TrafficMonitorDlg.cpp:267`） | 由 `CTaskBarDlg::ApplyWindowTransparentColor` 设置 WS_EX_LAYERED 与色键 |
| 通知区图标 | 主窗口负责（`m_ntIcon`、`theApp.m_notify_icons[]`） | 不持有 |
| 监控线程 | 启动并等待 `m_monitor_data_required` 标志 | 不参与 |

## 3. 显示内容：DisplayItem 与文本/数值/单位格式

### DisplayItem 枚举（TrafficMonitor/DisplayItem.h:5）

```cpp
enum DisplayItem {
    TDI_UP, TDI_DOWN, TDI_CPU, TDI_MEMORY, TDI_GPU_USAGE,
    TDI_CPU_TEMP, TDI_GPU_TEMP, TDI_HDD_TEMP, TDI_MAIN_BOARD_TEMP,
    TDI_HDD_USAGE, TDI_TOTAL_SPEED, TDI_CPU_FREQ, TDI_TODAY_TRAFFIC
};
```

`AllDisplayItems`（`DisplayItem.h:23`）汇总全部内置项；其中 `TDI_CPU_TEMP/TDI_GPU_TEMP/TDI_HDD_TEMP/TDI_MAIN_BOARD_TEMP` 在 `WITHOUT_TEMPERATURE` 宏下被剔除。

`CommonDisplayItem`（`DisplayItem.h:33`）将内置 `DisplayItem` 与插件 `IPluginItem*` 统一为同一个键值类型，提供 `<` / `==` 排序（`DisplayItem.cpp:19/31`）与若干查询方法。

### 标签/数值/单位的拼装（CommonData + DisplayItem.cpp）

- 标签文本（`CommonDisplayItem::DefaultString`，`DisplayItem.cpp:90`）：主窗口走带本地化字符串的 `IDS_UPLOAD_DISP`/`IDS_DOWNLOAD_DISP`/`IDS_MEMORY_DISP` 等，末尾追加 `": "`；任务栏窗口的 `TDI_UP/TDI_DOWN/TDI_TOTAL_SPEED` 走 `↑: `/`↓: `/`↑↓: `。
- `GetItemValueText(bool is_main_window)`（`DisplayItem.cpp:185`）按项返回数值：
  - `TDI_UP/DOWN/TOTAL_SPEED`：从 `CCommon::DataSizeToString` 出字符串，再依 `cfg_data->hide_unit`/`speed_unit` 决定是否追加 `/s`。
  - `TDI_CPU/MEMORY/GPU_USAGE/HDD_USAGE`：`CCommon::UsageToString`。
  - `TDI_MEMORY` 三态由 `cfg_data->memory_display` 决定（百分比/已用/可用，`CommonData.h:224`）。
  - 温度项走 `CCommon::TemperatureToString`；频率走 `CCommon::FreqToString`；`TDI_TODAY_TRAFFIC` 走 `CCommon::KBytesToString`。
- 主窗口对 `TDI_UP`/`TDI_DOWN` 还会在 `swap_up_down` 开启时交换显示（`DisplayItem.cpp:216`）。
- 数值-单位间隔由 `cfg_data->separate_value_unit_with_space` 控制（`CommonData.h:250`）。
- 任务栏窗口的样例字符串（用于宽度计算）在 `CommonDisplayItem::GetItemValueSampleText`（`DisplayItem.cpp:278`），遵守 `digits_number`/`hide_percent`/`hide_unit`/`speed_short_mode` 等任务栏专属设置。

### 主窗口的具体绘制流程（`CSkinFile::DrawInfo`）

`CSkinFile::DrawInfo(CDC* pDC, bool show_more_info)` 在 `SkinFile.cpp` 中实现：
- 选 `m_background_s` 或 `m_background_l`，按 `Layout::layout_items` 逐项调用 `DrawItemsInfo`。
- `DrawItemsInfo` 用 `CDrawCommon`/`CDrawCommonEx`（在 SkinFile.cpp 同时 include）做最终绘制，文本部分通过 `DrawSkinText`（`SkinFile.cpp:66`）按 `Alignment::LEFT/RIGHT/CENTER/SIDE` 排版。
- 颜色使用 `SkinInfo::text_color`（`specify_each_item_color` 控制是否逐项不同色）。
- 主窗口字体 `m_skin` 上的 `m_font` 来自 `CSkinFile::SetSettingData`（`SkinFile.h:108`）。

## 4. 显示设置对话框族

主窗口相关选项在 `COptionsDlg`（`OptionsDlg.h:13`）里以三个标签页承载：

### 4.1 MainWndSettingsDlg（OptionsDlg.h:26 / MainWndSettingsDlg.h:11）

- 派生自 `CTabDlg`（标签对话框基类），`m_data` 直接读写 `MainWndSettingData`（`MainWndSettingsDlg.h:20`）。
- 通过控件变量 `m_color_static`（`CColorStatic`）显示文本颜色样本，`OnStaticClicked` 进入 `CMFCColorDialogEx` 调色。
- 提供「主窗口设置」中"字体/颜色/显示文本/双击动作/单位/百分比/字节 vs 比特/始终置顶/鼠标穿透/锁定位置/允许超出屏幕边界/全屏时隐藏"等入口（响应函数声明见 `MainWndSettingsDlg.h:51-83`）。
- 关键响应：`OnBnClickedSetFontButton`（字体）、`OnBnClickedSpecifyEachItemColorCheck`（`specify_each_item_color`）、`OnBnClickedSwitchUpDownCheck`（`swap_up_down`）、`OnBnClickedFullscreenHideCheck`（`hide_main_wnd_when_fullscreen`）、`OnBnClickedDisplayTextSettingButton`（拉起 `CDisplayTextSettingDlg`）、`OnBnClickedResotreSkinDefaultButton`（清空覆盖回到皮肤默认）、`OnBnClickedAlwaysOnTopCheck`/`OnBnClickedMousePenetrateCheck`/`OnBnClickedLockWindowPosCheck`/`OnBnClickedAlowOutOfBorderCheck` 等。

### 4.2 DisplayTextSettingDlg（DisplayTextSettingDlg.h:8）

- 通过 `DispStrings& m_display_texts`（`DisplayTextSettingDlg.h:22`）编辑每个 `CommonDisplayItem` 的 `disp_str`（`CommonData.h:60` 处的 `DispStrings`）。
- 列表控件 `CListCtrlEx` 显示项名 + 文本。
- 支持「恢复默认」（`OnBnClickedRestoreDefaultButton`），右键菜单可针对单行恢复。
- 区分主窗口/任务栏窗口文本：`m_main_window_text`（`DisplayTextSettingDlg.h:25`）传入即标注，会影响某些项的恢复默认文案。
- `CDisplayTextSettingDlg` 的默认文本来自 `CommonDisplayItem::DefaultString`（`DisplayItem.cpp:90`）。

### 4.3 MainWndColorDlg（MainWndColorDlg.h:9）

- 接收 `std::map<CommonDisplayItem, COLORREF>` 引用（`MainWndColorDlg.h:14/24`），在 `CColorSettingListCtrl` 中按列显示"项名 + 色块"。
- 双击行触发 `CMFCColorDialogEx`（`MainWndColorDlg.cpp:87`），结果回写 `m_colors`。
- 项目集合来自 `CTrafficMonitorDlg::Instance()->GetCurSkin().GetSkinDisplayItems`（`MainWndColorDlg.cpp:65`），仅显示当前皮肤上启用的项。

### 4.4 IconSelectDlg（IconSelectDlg.h:8）

- 用于选择托盘图标。`m_icon_selected` 是 `m_notify_icons` 数组下标（`IconSelectDlg.h:37`），限定范围 `0..MAX_NOTIFY_ICON-1`（`IconSelectDlg.cpp:26`）。
- 控件：`m_preview_pic`（`CPictureStatic`）预览、`m_icon_select_combo`（下拉选图）、`m_auto_adapt_chk`（`m_atuo_adapt_notify_icon`，`IconSelectDlg.h:39`）。
- `DrawPreviewIcon`（`IconSelectDlg.h:46`）按 `m_icon_selected` 实际画出在 `m_notify_icons` 中的图标。
- 确认后写回 `m_cfg_data.m_notify_icon_selected` 与 `m_notify_icon_auto_adapt`（`TrafficMonitorDlg.cpp:2685-2694`）。

### 4.5 SetItemOrderDlg（SetItemOrderDlg.h:6）

- 编辑显示项的顺序与是否显示。`m_item_order`（`CTaskbarItemOrderHelper`）、`m_display_item`（`DisplayItemSet`）、`m_plugin_item`（`StringSet`）（`SetItemOrderDlg.h:31-34`）。
- `ShowItem` 刷新列表（`SetItemOrderDlg.h:39`），`OnBnClickedMoveUpButton` / `OnBnClickedMoveDownButton` 上下移，`OnCheckChanged` 切换显示/隐藏。
- 数据最终回到 `theApp.m_taskbar_data.item_order/display_item/plugin_display_item`（`TrafficMonitorDlg.cpp:2908-2910`），并触发 `m_tBarDlg->WidthChanged()`。

### 4.6 TaskbarItemOrderHelper（被主窗口与 SetItemOrderDlg 复用）

定义在 `TaskbarItemOrderHelper.h:4`：
- 内部 `m_item_order` 保存的是 `vector<int>`（下标引用），通过 `GetAllDisplayItemsWithOrder()`（`TaskbarItemOrderHelper.cpp:22`）按下标解析成 `CommonDisplayItem`。
- `NormalizeItemOrder`（`TaskbarItemOrderHelper.cpp:104`）剔除越界、删除重复、补齐缺失。
- `IsItemDisplayed`（`TaskbarItemOrderHelper.cpp:86`）根据 `IsHardwareEnable` 决定温度类项是否参与排序。
- `FromString`/`ToString`（`TaskbarItemOrderHelper.cpp:40/52`）以逗号分隔字符串做 INI 持久化。

> 注：`SetItemOrderDlg` 主要服务于任务栏窗口的"显示项目"与"显示顺序"，主窗口的显示项目由皮肤 XML 决定（`CSkinFile::Layout`）。

## 5. 鼠标穿透 / 置顶 / 隐藏 / 锁位置 的落地

### 置顶（TrafficMonitorDlg.cpp:246 `SetAlwaysOnTop`）

- 入口跳过条件：主窗口处于隐藏（`m_cfg_data.m_hide_main_window`）或开启了"全屏时隐藏"且当前有全屏前台窗口。
- 实际动作：`SetWindowPos(&wndTopMost, ...)` 或 `&wndNoTopMost`，参数 `SWP_NOSIZE | SWP_NOMOVE`。
- 触发点：选项设置确认（`ApplySettings` 中 `is_always_on_top_changed` 时调用，`TrafficMonitorDlg.cpp:824`），以及 `OnTimer` 多次重置（首次 1 秒、5/9 秒、每 5 分钟；`TrafficMonitorDlg.cpp:1583/1650/1661`）。

### 鼠标穿透（TrafficMonitorDlg.cpp:267 `SetMousePenetrate`）

- 通过 `SetWindowLong(m_hWnd, GWL_EXSTYLE, ... | WS_EX_TRANSPARENT)` 或清除。
- 触发点：选项设置确认（`ApplySettings`）、首次启动 1 秒（`OnTimer`）、5/9 秒重置。
- 副作用：开启鼠标穿透且通知区图标未显示时，会强制 `AddNotifyIcon` 并置 `m_general_data.show_notify_icon = true`（`TrafficMonitorDlg.cpp:832-838`），否则右键菜单无入口。

### 透明度（TrafficMonitorDlg.cpp:228/233 `SetTransparency`）

- `WS_EX_LAYERED` 必加；PNG 皮肤走 `m_skin.SetAlpha(transparency * 255 / 100)`（`SkinFile.h:106`），BMP 皮肤走 `SetLayeredWindowAttributes(0, alpha, LWA_ALPHA)`。
- 启动时若 `m_hide_main_window` 或窗口位于 `(0,0)`，先 `SetTransparency(0)`，1 秒后由 `OnTimer` 的 `m_first_start` 分支再调用 `SetTransparency()` 恢复。

### 隐藏主窗口

- `m_cfg_data.m_hide_main_window` 为 true 时 `OnTimer` 中 `ShowWindow(SW_HIDE)`（`TrafficMonitorDlg.cpp:1586`），并在「全屏时隐藏」选项开启且检测到前台全屏时 `ShowWindow(SW_HIDE)`（`TrafficMonitorDlg.cpp:1625-1631`）。
- 前台全屏检测：`CCommon::IsForegroundFullscreen(h_current_monitor)`，每 1 秒重测一次（`TrafficMonitorDlg.cpp:1620-1624`）。

### 锁窗口位置

- 由 `m_main_wnd_data.m_lock_window_pos` 控制（`CommonData.h:271`）。`OnLButtonDown` 等的拖动逻辑在锁定时不移动；具体约束通过 `CheckWindowPos`（`TrafficMonitorDlg.cpp:333`）的反向校准配合实现：拖动结束后总是把窗口带回所有工作区之一内（详见下节）。

### 窗口位置回拉（TrafficMonitorDlg.cpp:279 `CalculateWindowMoveOffset` / 333 `CheckWindowPos`）

- `m_screen_rects` 来自 `Monitors` 枚举（`TrafficMonitorDlg.h:79`），保存 `rcWork`。
- 当 `m_alow_out_of_border = false` 时调用 `CheckWindowPos`：若主窗口不在任一 `rcWork` 内，按"距离最小方向"原则计算偏移并 `MoveWindow`。
- `screen_changed=true` 时按比例分配旧工作区中的相对位置（用于多屏热插拔）。
- 触发点：`OnTimer` 每 2 秒（`TrafficMonitorDlg.cpp:1683`）、`OnDisplaychange`、`OnExitSizeMove`、`OnMove` 间接路径、选项设置 `m_alow_out_of_border` 切换时（`OnAlowOutOfBorder`，`TrafficMonitorDlg.cpp:2700`）。

## 6. 通知区图标（Tray Icon）

### 资源

- `m_notify_icons[MAX_NOTIFY_ICON]`：`HICON[6]`，定义于 `TrafficMonitor.h:86`，`MAX_NOTIFY_ICON` 在 `stdafx.h:102` 定义为 6。
- 6 个图标分别在 `CTrafficMonitorDlg::OnInitDialog` 通过 `LoadImage(... IDI_NOFITY_ICON/IDI_NOFITY_ICON2/IDI_NOFITY_ICON3/IDR_MAINFRAME/IDI_NOFITY_ICON4/IDI_NOTIFY_ICON5 ...)` 加载，按 16x16 DPI 缩放（`TrafficMonitorDlg.cpp:1083-1088`）。
- `m_ntIcon`（`NOTIFYICONDATA`，`TrafficMonitorDlg.h:57`）持有 `hWnd`、当前 `hIcon`、回调消息 `MY_WM_NOTIFYICON`（`stdafx.h:69`）与提示文本。
- 鼠标提示内容由 `CTrafficMonitorDlg::UpdateNotifyIconTip`（`TrafficMonitorDlg.cpp:645`）组装：上行/下行速度、CPU、内存，条件性追加 GPU/CPU/HDD/主板温度与 GPU/HDD 占用率。

### `CDllFunctions` 与 `Shell_NotifyIconW` 封装

`DllFunctions.h:106` 处的 `CDllFunctions` 主要封装 `GetDpiForMonitor`（`DllFunctions.cpp:23`），同时以静态成员提供 `D3DCompile` / `DCompositionCreateDevice` / `CreateDXGIFactory2`（`DllFunctions.cpp:33-42`）—— 这些服务于 D2D 渲染路径。

`Shell_NotifyIconW` 本身未被 `CDllFunctions` 包装，主窗口直接通过 `::Shell_NotifyIcon(NIM_ADD/NIM_MODIFY/NIM_DELETE, &m_ntIcon)` 调用：
- `Add`：在 `OnInitDialog` 与需要补建图标的场景（`AddNotifyIcon`，`TrafficMonitorDlg.cpp:599`）。
- `Modify`：在 `UpdateNotifyIconTip`、`ShowNotifyTip`（`TrafficMonitorDlg.cpp:619`）中。
- `Delete`：`DeleteNotifyIcon`（`TrafficMonitorDlg.cpp:609`）或窗口销毁时。

### `AddNotifyIcon` / `DeleteNotifyIcon` 的副作用

两者都先关闭任务栏窗口、修改图标、再打开（`TrafficMonitorDlg.cpp:601-606/611-616`）—— 因为托盘在多显示器、换图标、改 `m_show_task_bar_wnd` 时需要重建上下文。

### `ShowNotifyTip`

- 先确保托盘可见（必要时 `AddNotifyIcon`）。
- 设 `NIF_INFO`，调用 `NIM_MODIFY` 弹出气泡（`TrafficMonitorDlg.cpp:619-643`）。
- 若启用了"通知一段时间后关闭图标"，用 `DELETE_NOTIFY_ICON_TIMER` 延迟 8s 删除（`stdafx.h` 中定义）。

### `AutoSelectNotifyIcon`

定义于 `TrafficMonitor.cpp:886`，仅在 Win10+ 上根据 `IsWindows10LightTheme` 决定：
- 浅色模式：白色图标（`m_notify_icon_selected == 0`）替换为黑色（4），深色图标（1）替换为（5）。
- 深色模式：反向。
- 由 `OnColorizationColorChanged`（头文件 `TrafficMonitorDlg.h:284`）或显式 `OnChangeNotifyIcon` 启用 `m_notify_icon_auto_adapt` 时驱动（`TrafficMonitorDlg.cpp:2688-2689`）。

## 7. 经典任务栏 vs Win11 任务栏（主窗口侧的协作）

主窗口在 `OpenTaskBarWnd`（`TrafficMonitorDlg.cpp:566`）中按以下规则选择派生类：

```cpp
if (theApp.m_win_version.IsWine())              m_tBarDlg = new CWineTaskbarDlg();
else if (theApp.IsWindows11Taskbar())           m_tBarDlg = new CWin11TaskbarDlg();
else                                             m_tBarDlg = new CClassicalTaskbarDlg();
```

随后根据 `CSupportedRenderEnums::GetAutoFitEnum()` 决定窗口模板：
- 走 `D2D1_WITH_DCOMPOSITION`：`Create(IDD_TASK_BAR_DIALOG_NOREDIRECTIONBITMAP, this)`（`TrafficMonitorDlg.cpp:586-588`）。
- 其它（含 `D2D1`、降级 `DEFAULT`）：`Create(IDD_TASK_BAR_DIALOG, this)`（`TrafficMonitorDlg.cpp:590-592`）。

之后 `ShowWindow(SW_SHOW)` 显示（`TrafficMonitorDlg.cpp:594`）。

`theApp.IsWindows11Taskbar()` 由 `CTrafficMonitorApp::CheckWindows11Taskbar`（`TrafficMonitor.h:167`）依据 `m_is_windows11_taskbar`（`TrafficMonitor.h:187`）决定，调用方主要在 `OpenTaskBarWnd`（`TrafficMonitorDlg.cpp:569`）和 `ApplyWindowTransparentColor` 入口（`TaskBarDlg.cpp:571`）。

`CWineTaskbarDlg` 仅作为 Wine 上的退化实现（`WineTaskbarDlg.cpp`）：不使用任务栏嵌入，而是把窗口贴到屏幕右下/左下并 `SetWindowPos(&wndTopMost, ...)`。

具体任务栏窗口行为见 `ui-taskbar-dialog.md`。
