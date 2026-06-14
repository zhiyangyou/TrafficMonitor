# 任务栏窗口（CTaskBarDlg 与派生类）主题文档

## 1. 类层级与共同职责

```
CTaskBarDlg : public CDialogEx                (TaskBarDlg.h:19)
 ├── CClassicalTaskbarDlg                       (ClassicalTaskbarDlg.h:3)
 ├── CWin11TaskbarDlg                           (Win11TaskbarDlg.h:3)
 └── CWineTaskbarDlg                            (WineTaskbarDlg.h:3)
```

`CTaskBarDlg` 是抽象基类（`InitTaskbarWnd`/`AdjustTaskbarWndPos`/`ResetTaskbarPos`/`CheckTaskbarOnTopOrBottom`/`GetParentHwnd` 五个纯虚函数，`TaskBarDlg.h:84-88`），按系统形态/版本委托给三个子类各自实现。

- 父窗口模板：`IDD_TASK_BAR_DIALOG`（默认）/ `IDD_TASK_BAR_DIALOG_NOREDIRECTIONBITMAP`（D2D + DComp 时使用，构造方在 `TrafficMonitorDlg.cpp:587`）。
- 窗口高度常量：`TASKBAR_WND_HEIGHT = DPI(32)`（`TaskBarDlg.h:14`）。
- 共享消息：`WM_TASKBAR_MENU_POPED_UP = WM_USER + 1004`（`TaskBarDlg.h:15`），右键菜单弹出时主窗口接收。
- 历史曲线采样间隔：`TASKBAR_GRAPH_STEP = 5`（`TaskBarDlg.h:17`），每隔 5 秒合并一次。

## 2. CTaskBarDlg 基类

### 2.1 字段总览（TaskBarDlg.h:92-156）

| 字段 | 用途 |
|------|------|
| `m_hTaskbar` / `m_rcTaskbar` | 任务栏窗口句柄与矩形 |
| `m_rect` / `m_window_width` / `m_window_height` | 当前任务栏窗口的矩形与计算后的尺寸 |
| `m_supported_render_enums` | 当前实例支持的渲染后端（详见 `render-api.md`） |
| `m_taskbar_draw_common_window_support` | `DefaultCLazyConstructableWithInitializer<CTaskBarDlgDrawCommonWindowSupport, CTaskBarDlgDrawCommonSupport&>`，从 `theApp.m_d2d_taskbar_draw_common_support.Get()` 取共享 support，按需构造（`TaskBarDlg.h:98-102`） |
| `m_d2d1_device_context_support` | 同上模式的 `CD2D1DeviceContextWindowSupport`（`TaskBarDlg.h:103-107`） |
| `ItemWidth` / `ItemWidthInfo` | 每项 label 与 value 的宽度（`TaskBarDlg.h:110-138`） |
| `m_item_widths` / `m_item_rects` | 项目宽度列表与最近一次绘制时项目矩形（用于 `CheckClickedItem`） |
| `m_map_history_data` / `m_history_data_count` | 占用率历史值（按 `CommonDisplayItem` 索引） |
| `m_connot_insert_to_task_bar` | `::SetParent` 失败的标志位 |
| `m_taskbar_on_top_or_bottom` | 由 `CheckTaskbarOnTopOrBottom` 决定 |
| `m_taskbar_dpi` | 任务栏窗口专用 DPI，通过 `DPI()` 模板族做缩放 |
| `m_font` | 字体（`CFont`），每次 `SetTextFont` 重建 |
| `m_pDC` | 缓存的窗口 `CDC*`，用于 `m_pDC->GetTextExtent` 计算宽度 |

### 2.2 关键方法

#### DPI 处理

- `DPI(UINT/INT/LONG pixel)` 与 `DPI(CRect&)`（`TaskBarDlg.cpp:640-661`）：按 `m_taskbar_dpi` 缩放，公式 `pixel * dpi / 96`。
- `CheckWindowMonitorDPIAndHandle`（`TaskBarDlg.h:55-76`）：一个 functor，调用 `theApp.DPIFromRect(GetRectForDpiCheck(), ...)`，缓存上次 DPI；DPI 变化时回调外部 handler。`GetRectForDpiCheck` 默认返回 `m_rcTaskbar`（`TaskBarDlg.cpp:625`）。

#### `OnInitDialog`（TaskBarDlg.cpp:976）

1. `SetWindowText(TASKBAR_WINDOW_NAME)`、设置 `WS_EX_TOOLWINDOW`。
2. 检测 Windows Web Experience：`WindowsWebExperienceDetector::IsDetected()` 写入 `theApp.m_taskbar_data.is_windows_web_experience_detected`。
3. `DisableRenderFeatureIfNecessary(m_supported_render_enums)`（`TaskBarDlg.cpp:495-508`）：根据 `IsTaskbarTransparent()` / `auto_set_background_color` / `disable_d2d` / `update_layered_window_error_code` 收敛可用渲染后端。
4. `FindTaskbarHandle`（`TaskBarDlg.cpp:670`）：优先在副显示器任务栏列表中按 `secondary_display_index` 取；否则 `FindWindow(_T("Shell_TrayWnd"), NULL)`。
5. `ApplyWindowTransparentColor`（`TaskBarDlg.cpp:568`）：按渲染类型设置 `WS_EX_LAYERED` 与 `LWA_COLORKEY`（GDI 路径），D2D 路径不设色键。
6. `InitTaskbarWnd()`（虚函数，子类实现）：在 Win11/经典任务栏下找子窗口。
7. `::SetParent(this->m_hWnd, GetParentHwnd())`：将任务栏窗口嵌入二级容器，失败则 `m_connot_insert_to_task_bar = true`。
8. `DPIFromRect(GetRectForDpiCheck(), ...)` 或 `theApp.GetDpi()` 设置 `m_taskbar_dpi`（仅 Win8.1+ 走位置查询）。
9. `SetTextFont`、`m_pDC->SelectObject(&m_font)`、`CheckTaskbarOnTopOrBottom`、`CalculateWindowSize`、`AdjustWindowPos(true)`。
10. `SetBackgroundColor(m_taskbar_data.back_color)`：MFC 默认背景色。
11. `m_tool_tips.Create(this, TTS_ALWAYSTIP)` + `SetToolTipsTopMost`。
12. `SetTimer(TASKBAR_TIMER, 1000, NULL)`：1 秒 tick，触发宽度重算与 `WidthChanged()`。

#### `OnTimer`（TaskBarDlg.cpp:1230）

每秒：
- `m_menu_popuped` 时强制 `m_tool_tips.Pop()`。
- `CalculateWindowSize` 重算后与上次 `m_window_width/height` 对比，变化时 `WidthChanged()`，最终触发 `AdjustWindowPos` 重新摆位。

#### `OnPaint`（TaskBarDlg.cpp:1294）

调用 `ShowInfo(&dc)` 一次绘制流程，内含完整的 D2D 资源初始化与异常处理链（见 `render-api.md`）：
- `CD3D10Exception1` → 请求重建 D3D10.1 设备。
- `CD2D1Exception` → 请求重建 D2D1 设备。
- `CDCompositionException` → 请求重建 DComp 设备。
- `CHResultException` → 仅记录日志。
- `std::runtime_error` → 累计错误计数，超过 `MAX_D2D1_RENDER_ERROR_COUNT`（77，`TaskBarDlgDrawCommon.h:79`）时永久关闭 D2D 渲染并弹窗（`TaskBarDlgDrawCommon.cpp:138-147`）。

#### `ShowInfo(CDC*)`（TaskBarDlg.cpp:60）

- 若 `m_supported_render_enums.IsD2D1Enabled()` 且 `update_layered_window_error_code` 非 0，则降级（D2D1 关、仅 DComp；DComp 也没有就退到 DEFAULT；`TaskBarDlg.cpp:70-89`）。这是 D2D 路径下 `UpdateLayeredWindowIndirect` 失败的恢复路径。
- 通过 `GetInterfaceFromAllInvolvedDrawCommonObjects` 按当前 `render_type` 选 `CDrawDoubleBuffer`（DEFAULT）/ `CTaskBarDlgDrawBuffer`（D2D1）/ `CTaskBarDlgDrawBufferUseDComposition`（D2D1 + DComp），并构造对应 `CDrawCommon` / `CTaskBarDlgDrawCommon`。
- 逐项（按 `m_item_widths` 顺序）计算矩形并 `DrawDisplayItem` / `DrawPluginItem`。

#### `DrawDisplayItem`（TaskBarDlg.cpp:272）

- 写 `m_item_rects[type] = rect` 供命中测试使用。
- 取颜色：`m_taskbar_data.specify_each_item_color` 为真时按 `text_colors[type]`；否则取 map 的首项作为统一颜色。
- 占用图 / 状态条：依据 `m_taskbar_data.show_status_bar`、`show_netspeed_figure` 与 `cm_graph_type` 决定调用 `TryDrawStatusBar` 或 `TryDrawGraph`（柱状图 vs 折线图）。`TryDrawStatusBar`（`TaskBarDlg.cpp:510`）画一个按百分比填充的矩形，可选 `show_graph_dashed_box` 虚线边框。
- 文本：标签从 `m_taskbar_data.disp_str.GetConst(type)` 取，`GetItemValueText(false)` 取数值；`value_right_align` 决定对齐。

#### `DrawPluginItem`（TaskBarDlg.cpp:381）

- `item->IsDrawResourceUsageGraph()` 为 true 且 plugin API ≥ 6 时，与内置项一样画状态条/折线图。
- `item->IsCustomDraw()` 为 true：通知插件当前 label/value 文本色（通过 `OnExtenedInfo(EI_LABEL_TEXT_COLOR/EI_VALUE_TEXT_COLOR)`），再调用 `item->DrawItem` 自绘（HDC 通过 `typeid(drawer)` 区分 `CDrawCommon` 与 `CTaskBarDlgDrawCommon`，后者通过 `ExecuteGdiOperation` 在 GDI 互操作纹理上执行）。

#### `CalculateWindowSize`（TaskBarDlg.cpp:812）

- 对每个显示项：标签宽度 = `m_pDC->GetTextExtent(disp_str)`；数值宽度 = `m_pDC->GetTextExtent(GetItemValueSampleText(false))`。插件项按 `IsCustomDraw` 区别处理（自定义项宽 = `theApp.m_plugins.GetItemWidth(plugin, m_pDC)`，否则按 `GetItemLableText/GetItemValueSampleText`）。
- 顺序与过滤由 `theApp.m_taskbar_data.item_order.GetAllDisplayItemsWithOrder()` 提供，再过滤 `IsTaksbarItemDisplayed(item)`。
- 任务栏水平排列时：`m_window_width = Σ TotalWidth + item_space * item_count`（`TaskBarDlg.cpp:875-882`）。
- 任务栏在屏幕两侧时：取所有项的 `TotalWidth` 最大值。
- 水平排列时高度 `TASKBAR_WND_HEIGHT * 2 / 3`，否则完整 `TASKBAR_WND_HEIGHT`；任务栏在屏幕两侧时按项数 × `TASKBAR_WND_HEIGHT / 2` 累加。

#### `AdjustWindowPos(bool force_adjust)`（TaskBarDlg.cpp:525）

虚函数，由子类实现具体位置策略（`ClassicalTaskbarDlg::AdjustTaskbarWndPos`、`Win11TaskbarDlg::AdjustTaskbarWndPos`）。基类的统一行为：

1. 检测是否 `force_adjust || m_is_width_changed`，是则 `ResetTaskbarPos()`。
2. `::GetWindowRect(m_hTaskbar, m_rcTaskbar)`。
3. `CheckTaskbarOnTopOrBottom()`，若结果变化或 `force_adjust` 则重算窗口尺寸。
4. 调用子类 `AdjustTaskbarWndPos(force_adjust)`。
5. 嵌入失败（`m_connot_insert_to_task_bar`）：将窗口相对父窗口移动到绝对坐标；如当前焦点在任务栏上则 `SetWindowPos(&wndTopMost, ...)` 置顶。

#### `ApplyWindowTransparentColor`（TaskBarDlg.cpp:568）

根据 `m_supported_render_enums.GetAutoFitEnum()`：
- `D2D1_WITH_DCOMPOSITION`：不设 `WS_EX_LAYERED`，由 DComp 合成。
- `D2D1`：仅设 `WS_EX_LAYERED`。
- `DEFAULT`：设 `WS_EX_LAYERED` + `SetLayeredWindowAttributes(transparent_color, 0, LWA_COLORKEY)`（GDI 色键）。
- 不透明：清除 `WS_EX_LAYERED`。
- Win11 任务栏特殊处理：若 `transparent_color == 0 && back_color == 0`，修正为 1（避免深色模式右键菜单被屏蔽，`TaskBarDlg.cpp:573-577`）。

#### `WidthChanged` / `IsTaskbarCloseToIconEnable` / 静态助手

- `WidthChanged()`：置 `m_is_width_changed = true`，下次 `OnTimer` 触发 `AdjustWindowPos(force_adjust=true)`。
- `IsTaskbarCloseToIconEnable(bool taskbar_wnd_on_left)`（`TaskBarDlg.cpp:968`）：仅 Win11 中心对齐且任务栏窗口在左时允许"靠近图标"。
- `IsItemShow` / `IsShowCpuMemory` / `IsShowNetSpeed`：根据 `m_taskbar_data.display_item` 集合判断。

## 3. CClassicalTaskbarDlg（经典模式）

### 关键字段（ClassicalTaskbarDlg.h:16-25）

- `m_hBar`：任务栏二级容器 `ReBarWindow32` 的句柄；找不到时退到 `WorkerW`（`ClassicalTaskbarDlg.cpp:73-74`）。
- `m_hMin`：最小化窗口 `MSTaskSwWClass`/`MSTaskListWClass`（`ClassicalTaskbarDlg.cpp:75-77`）。
- `m_rcBar` / `m_rcMin` / `m_rcMinOri`：二级容器与最小化窗口的当前/初始矩形。
- `m_left_space` / `m_top_space`：任务栏在水平/垂直时 `MSTaskSwWClass` 相对 `ReBarWindow32` 的偏移。
- `m_last_width` / `m_last_height`：宽度/高度变化检测缓存。

### `InitTaskbarWnd`（ClassicalTaskbarDlg.cpp:70）

- 找 `ReBarWindow32` → `MSTaskSwWClass`（`WorkerW` / `MSTaskListWClass` 兜底）。
- 记录 `m_rcMin` / `m_rcBar` / `m_left_space` / `m_top_space`。
- `GetParentHwnd()` 返回 `m_hBar`（二级容器），即嵌入目标。

### `CheckTaskbarOnTopOrBottom`（ClassicalTaskbarDlg.cpp:104）

通过 `m_hTaskbar` 的宽高比判断：`rect.Width() >= rect.Height()` 即上下任务栏。

### `AdjustTaskbarWndPos`（ClassicalTaskbarDlg.cpp:4）

- 任务栏在顶部/底部：把 `MSTaskSwWClass` 宽度减去 `m_rect.Width()` 推到 `m_left_space`，任务栏窗口 `MoveWindow` 到 `m_left_space` 或右侧。
- 任务栏在屏幕左右两侧：同样挪动 `MSTaskSwWClass` 的高度。
- Win7 水平排列时 Y 方向 +1 像素（`ClassicalTaskbarDlg.cpp:36-37`）。
- 失败兜底在 `CTaskBarDlg::AdjustWindowPos` 末尾统一处理。

### `ResetTaskbarPos`（ClassicalTaskbarDlg.cpp:86）

恢复 `m_rcMinOri` 时的 `MSTaskSwWClass` 大小，用于程序关闭时把任务栏还原。

## 4. CWin11TaskbarDlg（Win11 模式）

### 关键字段（Win11TaskbarDlg.h:15-21）

- `m_hNotify`：`TrayNotifyWnd`（系统时间/托盘区）。
- `m_hStart`：`Start` 按钮。
- `m_rcNotify` / `m_rcStart`：上述窗口的矩形。
- `m_last_notify_width` / `m_last_start_pos`：宽度/位置缓存。

### `InitTaskbarWnd`（Win11TaskbarDlg.cpp:89）

- `FindWindowEx(m_hTaskbar, 0, L"TrayNotifyWnd", NULL)`。
- `FindWindowEx(m_hTaskbar, nullptr, L"Start", NULL)`。

### `CheckTaskbarOnTopOrBottom`（Win11TaskbarDlg.cpp:105）

直接置 `m_taskbar_on_top_or_bottom = true`（Win11 任务栏始终水平）。

### `AdjustTaskbarWndPos`（Win11TaskbarDlg.cpp:5）

1. 强制 `m_rect` 大小并按 `m_hStart` 位置调整 X。
2. `force_adjust || m_rcNotify.Width() 变化 || m_rcStart.left 变化` 时进入重定位：
   - "任务栏窗口在右"或"Windows 11 任务栏左对齐"（`IsTaskbarCenterAlign()==false`）：
     - X = `m_rcNotify.left - m_rect.Width() + 2`。
     - 副屏没有 `TrayNotifyWnd` 时回退到 `m_rcTaskbar.Width() - DPI(88)`，否则用 `m_taskbar_data.taskbar_right_space_win11`。
     - 勾选 `avoid_overlap_with_widgets` 且 `IsTaskbarWidgetsBtnShown` 且 `!IsTaskbarCenterAlign` 时额外减去 `taskbar_left_space_win11`。
   - "任务栏窗口在左"且中心对齐：
     - 勾选 `tbar_wnd_snap` 时 X = `m_rcStart.left - m_rect.Width() - 2`（贴向开始按钮）。
     - 否则 X = 2（再叠加 `taskbar_left_space_win11`）。
   - 最后 `m_rect.left += DPI(window_offset_left)`。
   - Y = `(m_rcStart.Height() - m_rect.Height()) / 2 + (m_rcTaskbar.Height() - m_rcStart.Height()) + DPI(window_offset_top)`（`Win11TaskbarDlg.cpp:78`）。最后那行加号专门为触屏设备 build 22621 后的任务栏位置 bug 提供偏移。
3. `MoveWindow(m_rect)`。

### `ResetTaskbarPos`（Win11TaskbarDlg.cpp:96）

空实现——Win11 任务栏不需要恢复二级容器。

### `GetParentHwnd`（Win11TaskbarDlg.cpp:100）

直接返回 `m_hTaskbar`（即 `Shell_TrayWnd`），让 `::SetParent` 把任务栏窗口嵌到整个任务栏上。

## 5. CWineTaskbarDlg

`WineTaskbarDlg.h:3` 与同名 .cpp 给 Wine 平台提供兜底：
- `InitTaskbarWnd`：`SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE)`。
- `AdjustTaskbarWndPos`：`MoveWindow` 到屏幕左下或右下角（按 `tbar_wnd_on_left`）。
- `ResetTaskbarPos`：空。
- `CheckTaskbarOnTopOrBottom`：置 `true`。
- `GetParentHwnd`：`GetDesktopWindow()->GetSafeHwnd()`。

## 6. 嵌入到任务栏的细节

### 经典模式下的二级容器

- `m_hBar = FindWindowEx(m_hTaskbar, 0, L"ReBarWindow32", NULL)`（`ClassicalTaskbarDlg.cpp:72`）。
- `m_hMin = FindWindowEx(m_hBar, 0, L"MSTaskSwWClass", NULL)`（`ClassicalTaskbarDlg.cpp:75`），失败回退 `MSTaskListWClass`。
- `::SetParent(this->m_hWnd, GetParentHwnd())` 在 `CTaskBarDlg::OnInitDialog`（`TaskBarDlg.cpp:999`）执行。
- 嵌入失败时 `m_connot_insert_to_task_bar=true`，主窗口每 10 秒重试一次（`TrafficMonitorDlg.cpp:1690-1716`），超过 `WARN_INSERT_TO_TASKBAR_CNT` 后弹出错误对话框。

### DPI 适配

- `m_taskbar_dpi` 在 OnInitDialog 时通过 `DPIFromRect(m_rcTaskbar, ...)` 或 `theApp.GetDpi()` 设置（`TaskBarDlg.cpp:1001-1013`）。
- 后续所有尺寸/位置通过 `DPI()` 模板族（`TaskBarDlg.cpp:640-661`）按 `m_taskbar_dpi / 96` 缩放。
- 父窗口 DPI 变化通过 `CheckWindowMonitorDPIAndHandle` funtor 检测（`TaskBarDlg.h:55-76`），主窗口在 `OnDpichanged` 中以延迟 500ms 的方式更新 DPI 并通知任务栏子窗口（`TrafficMonitorDlg.cpp:2786-2814`）。

### Win11 特殊处理

- `m_is_secondary_display`（`TaskBarDlg.h:149`）标记是否在副显示器任务栏上，副屏没有 `TrayNotifyWnd` 时回退到 `m_rcTaskbar.Width() - DPI(88)`（`Win11TaskbarDlg.cpp:36-37`）。
- `WindowsWebExperienceDetector::IsDetected()`（`WindowsWebExperienceDetector.cpp:12`）通过 WinRT 查询 `MicrosoftWindows.Client.WebExperience` 包是否存在，结果写入 `m_taskbar_data.is_windows_web_experience_detected`，被主窗口用于决定是否启用 `avoid_overlap_with_widgets` 行为。
- 任务栏窗口的初始位置完全在 `CWin11TaskbarDlg::AdjustTaskbarWndPos` 中计算，不依赖 `ReBarWindow32` 之类二级容器。

## 7. 显示项的顺序、隐藏、宽度、文本/数值颜色

### 顺序与过滤

- `theApp.m_taskbar_data.item_order`（`CTaskbarItemOrderHelper`）：决定遍历顺序。
- `theApp.m_taskbar_data.display_item`（`DisplayItemSet`）：内置项的"显示/隐藏"集合。
- `theApp.m_taskbar_data.plugin_display_item`（`StringSet`）：插件项的 ID 集合。
- `CTaskbarItemOrderHelper::IsItemDisplayed`（`TaskbarItemOrderHelper.cpp:86`）按 `IsHardwareEnable` 过滤温度类项。
- `theApp.IsTaksbarItemDisplayed(CommonDisplayItem)` 在 `CalculateWindowSize` 中过滤（`TaskBarDlg.cpp:862`）。

### 宽度计算

- 见 `CalculateWindowSize`（`TaskBarDlg.cpp:812`）。
- 任务栏水平/垂直布局由 `m_taskbar_on_top_or_bottom` 决定；水平时按 `horizontal_arrange` 决定逐项还是"两两成对上下堆叠"。
- 自定义绘制项宽 = `theApp.m_plugins.GetItemWidth(plugin, m_pDC)`。

### 文本/数值颜色

- `m_taskbar_data.text_colors` 是 `std::map<CommonDisplayItem, TaskbarItemColor>`，每项存 label 与 value 两种颜色（`CommonData.h:280`）。
- `specify_each_item_color = true` 时按项取色；否则取 map 的首项颜色（`TaskBarDlg.cpp:278-287/411-419`）。
- 标签、值各自通过 `drawer.DrawWindowText` 绘制，文本与单位的具体格式由 `CommonDisplayItem::GetItemValueText`（`DisplayItem.cpp:185`）生成；样例字符串 `GetItemValueSampleText`（`DisplayItem.cpp:278`）用于宽度估算。

### 状态条 / 折线图

- `TryDrawStatusBar`（`TaskBarDlg.cpp:510`）：填充 `usage_percent` 比例的颜色矩形 + 可选虚线框。
- `TryDrawGraph`（`TaskBarDlg.cpp:1403`）：从 `m_map_history_data[item_type]` 末尾向前画线，每隔 `TASKBAR_GRAPH_STEP`（5）秒采一个均值（`AddHisToList`，`TaskBarDlg.cpp:1350`）；网速图最大值由 `GetNetspeedFigureMaxValueInBytes`（`CommonData.cpp:259`）决定。
- 颜色由 `m_taskbar_data.GetUsageGraphColor()`（`CommonData.cpp:267`）决定，主题色跟随时通过 `RGBtoHSL`/`HLStoRGB` 调整亮度。
- 折线图开关 `cm_graph_type`、虚线框 `show_graph_dashed_box`、网速图开关 `show_netspeed_figure` 全部来自 `TaskBarSettingData`。

## 8. 设置对话框

### 8.1 TaskBarSettingsDlg（TaskBarSettingsDlg.h:11 / OptionsDlg.h:27）

- 顶层 `CTabDlg`，挂在 `COptionsDlg` 的第二个标签页。
- `m_data` 直接读写 `TaskBarSettingData`。
- 提供 D2D/GDI 切换（`OnBnClickedGdiRadio`/`OnBnClickedD2dRadio`）、Win11 设置入口（`OnBnClickedWin11SettingsButton`）、副显示器选择、默认样式应用（`OnBnClickedDefaultStyleButton`）、占用图样式、显示文本设置（`OnBnClickedDisplayTextSettingButton` 拉起 `CDisplayTextSettingDlg`，构造参数 `m_data.disp_str`）、显示顺序（`OnBnClickedSetOrderButton` 拉起 `CSetItemOrderDlg`）、自动浅色主题（`OnBnClickedAutoAdaptLightThemeCheck`）、自动背景色（`OnBnClickedAutoSetBackColorCheck`）、水平排列（`OnBnClickedHorizontalArrangeCheck`）、数值右对齐、显示状态条、状态条虚线框、双击动作、字体、记忆体显示、单位/百分比/字节 vs 比特、显示工具提示、是否副显示器等。

### 8.2 Win11TaskbarSettingDlg（Win11TaskbarSettingDlg.h:8）

- 接收 `TaskBarSettingData&` 引用（`Win11TaskbarSettingDlg.h:22`）。
- 提供 `window_offset_top`、`window_offset_left` 与 `widgets_width` 三个 `CSpinEdit`。
- 「恢复默认」按钮（`OnBnClickedRestoreDefaultButton`）。

### 8.3 TaskbarColorDlg（TaskbarColorDlg.h:9）

- 接收 `std::map<CommonDisplayItem, TaskbarItemColor>` 引用——每项 label 与 value 两种颜色。
- `CColorSettingListCtrl` 显示项名 + 两个色块；双击行弹 `CMFCColorDialogEx` 改色。

### 8.4 TaskbarDefaultStyle（TaskbarDefaultStyle.h:8 / `theApp.m_taskbar_default_style`）

- 内置 4 套预设：`TASKBAR_DEFAULT_STYLE_NUM = 4`（`TaskbarDefaultStyle.h:4`），索引 2/3 标记为浅色模式预设（`TASKBAR_DEFAULT_LIGHT_STYLE`）。
- 每个预设保存 `text_colors`/`back_color`/`transparent_color`/`status_bar_color`/`specify_each_item_color`（`TaskbarDefaultStyle.h:11-18`）。
- `LoadConfig`/`SaveConfig`（`TaskbarDefaultStyle.cpp:16/57`）通过 `CSettingsHelper` 写 INI。
- `ApplyDefaultStyle(int, TaskBarSettingData&)`（`TaskbarDefaultStyle.cpp:85`）把当前预设拷到 `TaskBarSettingData`。
- `ModifyDefaultStyle(int, TaskBarSettingData&)`（`TaskbarDefaultStyle.cpp:101+`）反向把当前数据保存为预设。
- `IsTaskBarStyleDataValid`（`TaskbarDefaultStyle.h:30`）检查预设是否合法（避免全黑文字+全黑背景）。
- 自动主题切换在 `TaskBarSettingsDlg` 与 `theApp.m_taskbar_data` 之间经 `auto_adapt_light_theme` / `auto_save_taskbar_color_settings_to_preset` 协作。

## 9. TaskbarHelper 与 TaskbarItemOrderHelper

- `CTaskbarHelper`（`TaskbarHelper.h:3`）提供三个静态函数：
  - `GetAllSecondaryDisplayTaskbar`（`TaskbarHelper.cpp:82`）：先 `EnumDisplayMonitors` 收集显示器，再 `EnumWindows` 找 `Shell_SecondaryTrayWnd` 类，按显示器顺序排序输出。
  - `GetDisplayNum`（`TaskbarHelper.cpp:104`）：显示器总数。
  - `GetSecondaryTaskbarNum`（`TaskbarHelper.cpp:111`）：副显示器任务栏数量。
- `CTaskbarItemOrderHelper`（`TaskbarItemOrderHelper.h:4`）的角色见 `ui-main-dialog.md` §4.6；它负责顺序的归一化（`NormalizeItemOrder`）与 INI 持久化。

## 10. 与主窗口的协作消息

- `WM_TASKBAR_WND_CLOSED`（`stdafx.h` 中定义）：任务栏窗口 `OnClose`（`TaskBarDlg.cpp:1429`）向主窗口发送，主窗口收到后清 `m_show_task_bar_wnd` 并按需补建托盘图标。
- `WM_REOPEN_TASKBAR_WND`：用户在任务栏右键菜单切换"显示项目"中的某项时（`OnCommand`，`TaskBarDlg.cpp:1267-1276`）发出；主窗口 `OnReopenTaksbarWnd`（`TrafficMonitorDlg.cpp:2874`）执行 `CloseTaskBarWnd + OpenTaskBarWnd`。
- `WM_TASKBAR_MENU_POPED_UP = WM_USER + 1004`（`TaskBarDlg.h:15`）：任务栏菜单弹出时通过 `::SendMessage(theApp.m_pMainWnd->GetSafeHwnd(), WM_TASKBAR_MENU_POPED_UP, ...)` 通知主窗口（`TaskBarDlg.cpp:1152`）；主窗口 `OnTaskbarMenuPopedUp` 处理（`TrafficMonitorDlg.h:255`）。
- `WM_TABLET_QUERYSYSTEMGESTURESTATUS`：被任务栏窗口与主窗口都吞掉返回 0，避免触屏设备被识别为平板而禁用任务栏 UI。
- `WM_EXITMENULOOP`：`OnExitmenuloop` 把 `m_menu_popuped` 置回 false，提示可以再次 `Pop` 工具提示。

## 11. 鼠标交互

- `OnLButtonDblClk`（`TaskBarDlg.cpp:1192`）：先看点击项是否插件（`CheckClickedItem`），是则交 `IPluginItem::OnMouseEvent(MT_DBCLICKED, ...)`；否则按 `m_taskbar_data.double_click_action` 分发（与主窗口的双击动作共用 `DoubleClickAction` 枚举）。
- `OnRButtonUp`（`TaskBarDlg.cpp:1064`）：弹 `m_taskbar_menu`，是插件项则替换最后一项的菜单文本为插件名、设置插件图标。
- `OnMouseWheel`（`TaskBarDlg.cpp:1466`）：把 120/-120 delta 转成 `MT_WHEEL_UP/DOWN` 发给插件。
- `PreTranslateMessage`（`TaskBarDlg.cpp:1155`）：拦截 ESC/回车；转发键盘事件给 `IPluginItem::OnKeboardEvent`；转发鼠标事件到 `m_tool_tips`。
- `OnCommand`（`TaskBarDlg.cpp:1255`）：选择网络连接、显示插件项、调用插件命令等。
