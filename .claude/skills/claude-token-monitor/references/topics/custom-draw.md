# Custom Draw

本插件 4 个 item 全部 `IsCustomDraw()=true`，自绘滚动柱形图。原因：主程序自带滚动柱形图路径对插件是坏的（详见下文）。自绘时拿到的是 HDC + 矩形 + dark_mode flag，由插件直接用 `CDC` API 画柱状图。

## 为什么必须自绘

主程序在 `TrafficMonitor/TaskBarDlg.cpp:381-485` 的 `DrawPluginItem` 里对插件 item 有两条路径：

1. `theApp.m_taskbar_data.cm_graph_type == true`（滚动柱形图模式）→ 调 `AddHisToList(item, figure_value)` + `TryDrawGraph(drawer, rect, item)`
2. 否则（柱状图模式）→ 调 `TryDrawStatusBar(drawer, rect, figure_value)`

这两条路径只在 `IsDrawResourceUsageGraph()` 返回非 0 时才进（line 391 闸）。

路径 1 看起来对插件可用，但函数签名不匹配：
- `AddHisToList` 的声明在 `TaskBarDlg.h:163`：`void AddHisToList(CommonDisplayItem item_type, int current_usage_percent);`
- `TryDrawGraph` 的声明在 `TaskBarDlg.h:32`：`void TryDrawGraph(IDrawCommon& drawer, const CRect& value_rect, CommonDisplayItem item_type);`
- 函数实现在 `TaskBarDlg.cpp:1350` 和 `TaskBarDlg.cpp:1403`，形参都是 `CommonDisplayItem item_type`，**不是 `IPluginItem*`**

`TaskBarDlg.cpp:397-398` 那两行调用传 `IPluginItem* item` 给期望 `CommonDisplayItem` 形参的函数，**不能通过编译**。这意味着 `cm_graph_type==true`（滚动模式）时插件只画死图，柱形图路径在源码层就是断的。

参考：`PluginDemo/PluginDemo.h` 没有 item 实现 `IsDrawResourceUsageGraph()`（默认返回 0，line 145），所以 PluginDemo 也不走这条路径，自绘是 PluginDemo 唯一的画法。

结论：主程序的滚动柱形图路径对所有插件都不工作（不是只对本插件）。本插件用 `IsCustomDraw=true` 自绘，是当前唯一可行的滚动柱形图实现路径。

## DrawItem 收到的参数

`PluginInterface.h:76` 声明：

```cpp
virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode);
```

调用方 `TaskBarDlg.cpp:442`（GDI 路径）：

```cpp
item->DrawItem(p_dc->GetSafeHdc(), rect.left, rect.top, rect.Width(), rect.Height(), background_brightness < 128);
```

调用方 `TaskBarDlg.cpp:448-455`（D2D 路径）：

```cpp
ref_d2d1_drawer.ExecuteGdiOperation(rect,
    [item, rect, background_brightness](HDC gdi_dc) {
        item->DrawItem(gdi_dc, rect.left, rect.top, rect.Width(), rect.Height(), background_brightness < 128);
    });
```

注意：D2D 路径下主程序会把 `gdi_dc` 包在 `ExecuteGdiOperation` 里短暂切到 GDI 兼容上下文，插件拿到的就是普通 `HDC`。两种渲染路径（`CDrawCommon` GDI / `CTaskBarDlgDrawCommon` D2D）插件侧接口完全一致。

## dark_mode 计算

`TaskBarDlg.cpp:430`：

```cpp
const COLORREF& bk{ theApp.m_taskbar_data.back_color };
int background_brightness{ (GetRValue(bk) + GetGValue(bk) + GetBValue(bk)) / 3 };
```

`background_brightness < 128` → `dark_mode=true`，否则 `false`。

判定用 RGB 三通道平均，跟 Windows 深色模式的 API 不挂钩，纯粹用任务栏背景色亮度。背景色由主程序设置（任务栏透明 / 黑色 / 自定义色）。

## 自绘布局

参数 `w` 是主程序分配的矩形宽度，`GetItemWidth()` 返回 40 是最小值，主程序按 DPI 自动放大，实际 `w` 可能 50-80px。

布局策略：右侧 40px 给数字（"12.3k/s"），剩余宽度 = 柱状图区。

```
+-----------------------+--------+
|     柱状图区           |  数字  |
| (w - 40) px           | 40 px  |
+-----------------------+--------+
```

`y` 到 `y+h` 是垂直方向。柱状图每根柱 1px 宽，整条柱状图宽 = `w - 40`，所以最多 `w-40` 根柱。`CRingBuffer<float, 128>` 容量 128，永远不会被实际宽度（~50px）截断。

## 柱状图渲染

按 CRingBuffer 顺序遍历，每根柱 1px 宽：

```cpp
CRect bar_rect(x, y, x + w - 40, y + h);
CRect text_rect(x + w - 40, y, x + w, y + h);
n = m_history.Size();
for (i = 0; i < n; i++) {
    v = m_history.At(i);          // 0=oldest, n-1=newest
    col_x = bar_rect.right - (i + 1);
    col_h = (int)(v * bar_rect.Height());
    col_y_top = bar_rect.bottom - col_h;
    pDC->FillSolidRect(CRect(col_x, col_y_top, col_x + 1, bar_rect.bottom), color);
}
```

从右向左遍历：i=0（最老）画在最左，i=n-1（最新）画在最右，跟随时间从左向右"滚动"。`v` 是 0.0~1.0 归一化值。

## 颜色

4 类 token 各一色。颜色来源 `SettingData`：

| Token 类 | 默认浅色 | 默认深色 |
|---|---|---|
| `input_tokens` | `RGB(0, 200, 80)`（绿） | `RGB(0, 130, 50)`（深绿） |
| `cache_creation_input_tokens` | `RGB(230, 180, 0)`（黄） | `RGB(160, 120, 0)`（深黄） |
| `cache_read_input_tokens` | `RGB(80, 140, 230)`（蓝） | `RGB(50, 90, 160)`（深蓝） |
| `output_tokens` | `RGB(180, 80, 220)`（紫） | `RGB(120, 50, 150)`（深紫） |

深色模式用 deeper shade：每个通道各取约 65% 亮度。用户在选项对话框可以覆盖每个颜色（`CMFCColorButton` 控件）。

## 参考范例

`PluginDemo/CustomDrawItem.cpp:50-88` 的 `CCustomDrawItem::DrawItem` 是当前项目内最相近的自绘范例：

- line 53：`CDC* pDC = CDC::FromHandle((HDC)hDC);` — HDC 包装
- line 57-59：`COLORREF color{ dark_mode ? RGB(...) : RGB(...) };` — dark_mode 双色对照
- line 78-80：`pDC->FillSolidRect(rect, color);` — 实心矩形填充
- line 82-87：用 `DrawLine` 画刻度线

本插件的 `CTokenItem::DrawItem` 复用相同的 `CDC::FromHandle` / `FillSolidRect` 写法。

## 字体

`TaskBarDlg.cpp:443` 在 GDI 路径下会 `p_dc->SelectObject(&m_font)`，但这只对主程序接着画的文本生效。插件自绘时如果画文本（数字 "12.3k/s"），必须自己 `pDC->SelectObject` 一个字体。本插件用主程序的 `m_font` 引用需要在 `OnExtenedInfo(EI_LABEL_TEXT_COLOR, ...)` 时机去问主程序拿（line 434-435 主程序传的是文本颜色不是字体），所以插件侧自建一个 `CFont`（`CreatePointFont(80, L"Segoe UI")`）作为默认字体，亮度调到适合 `dark_mode` 的颜色（亮模式用深灰 `RGB(45,45,45)`，深色模式用浅灰 `RGB(225,225,225)`，参考 PluginDemo line 82 的 `color_scale`）。