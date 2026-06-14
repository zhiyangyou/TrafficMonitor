---
status: accepted
date: 2026-06-14
---

# 0002 — 渲染走 IsCustomDraw=true 自绘滚动柱形图

任务栏原生"滚动柱形图"路径在 TrafficMonitor 主程序里已经存在，但**对插件是坏的**。决策是每个 `CTokenItem` 返回 `IsCustomDraw=true`，在 `DrawItem(hDC, x, y, w, h, dark_mode)` 里**自己维护环形缓冲、自己画 HDC**，不走主程序那条"画资源占用图"的 helper。

证据链：

- `TrafficMonitor/TaskBarDlg.cpp:395-399` 在 `if (theApp.m_taskbar_data.cm_graph_type)` 分支里调用了：
  ```cpp
  AddHisToList(item, figure_value);
  TryDrawGraph(drawer, rect, item);
  ```
  其中 `item` 是 `IPluginItem*`。
- 但 `AddHisToList` 与 `TryDrawGraph` 的**声明**在 `TrafficMonitor/TaskBarDlg.h:163` / `TaskBarDlg.h:32`，**定义**在 `TaskBarDlg.cpp:1350` / `TaskBarDlg.cpp:1403`，签名全部是 `(CommonDisplayItem, ...)` 枚举类型。
- 编译器看不到 `IPluginItem* → CommonDisplayItem` 的隐式转换（且 RTTI 也不参与重载决议），所以 `cm_graph_type=true` 这两行**根本不被编译器接受**，滚动模式对插件从未真正工作过。
- 退路只有 `cm_graph_type=false` 分支调 `TryDrawStatusBar`，那只能画**单帧柱状图**——动不起来。

唯一能让柱形图动起来的方案是 `IsCustomDraw=true` 自绘。`DrawItem` 接收的 `dark_mode` 由主程序在 `TaskBarDlg.cpp:430` 根据背景色亮度判定，插件用它切深/浅色。

## Considered Options

- **A. `IsDrawResourceUsageGraph=1` + 走主程序 `TryDrawStatusBar`** — 否决。单帧柱状图，看不到速度趋势，违背"滚动柱形图"需求。
- **B. `IsCustomDraw=true` 自绘** — 采纳。插件独占 HDC、独占历史数据，颜色与宽高全自己控制。
- **C. 发 PR 给 TrafficMonitor 修这个 bug（让 helper 接 `IPluginItem*`）** — 否决。修需要同时改 `AddHisToList` 签名 + 重载 `m_map_history_data<std::map<CommonDisplayItem, ...>>`——超出"做成插件、不改主程序"的约束；且上游接受 PR 的周期不可控。

## Consequences

- 插件必须**自己**维护 4 个 `CRingBuffer<float, N>`（input / cache_creation / cache_read / output 各一）、自己归一化（60s 滑动 max、floor 100 tok/s 防 0 除）。
- `DrawItem` 内部直接拿 `CDC::FromHandle(hDC)`，走 GDI；不依赖主程序的 `IDrawCommon` 抽象层。
- 4 类 token 各一色，颜色存 `SettingData`、Options 对话框里 4 个 `CMFCColorButton` 可改。
- `dark_mode` 参数决定用亮色还是暗色变体；如果用户选项覆盖了颜色，选项优先。