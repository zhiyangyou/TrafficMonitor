# Plugin Contract

本插件是 `ITMPlugin` + `IPluginItem` 的实现，对接 `include/PluginInterface.h` 定义的二进制接口。本文档描述当前实现下，本插件对外暴露的接口形状、必须实现的虚函数、导出符号、GetItemId 稳定性约束。

## 接口定义文件

`G:\_GitSpace\_GitHub\TrafficMonitor.git\include\PluginInterface.h`

文件描述三个类：
- `IPluginItem`（line 9-152）— 单个显示项的接口
- `ITMPlugin`（line 158-324）— 插件实例的接口
- `ITrafficMonitor`（line 329-423）— 主程序反向传给插件的接口

## ITMPlugin 必须实现的虚函数

下列函数 `CClaudeTokenMonitorPlugin` 必须 override：

### `GetAPIVersion() const`（line 166）

返回 `7`。版本号含义在 line 432-454：
- `1`：首版
- `2`：新增 `ITMPlugin::GetTooltipInfo`
- `3`：新增 `IPluginItem::GetItemWidthEx` / `IPluginItem::OnMouseEvent`
- `4`：新增 `IPluginItem::OnKeboardEvent` / `IPluginItem::OnItemInfo`
- `5`：新增 `ITMPlugin::GetCommandName` / `GetCommandIcon` / `OnPluginCommand`
- `6`：新增 `IPluginItem::GetResourceUsageGraphType` / `GetResourceUsageGraphValue`
- `7`：新增 `ITMPlugin::OnInitialize`

主程序在 `PluginManager.cpp:83` 用 `PLUGIN_UNSUPPORT_VERSION` 做向下兼容闸；本插件返回 `7`，能用到所有 7 个版本的特性。

### `GetItem(int index)`（line 176）

按 index 返回 4 个 `IPluginItem*`，依次是：
- `0` → `m_token_in`（CTokenItem，Input）
- `1` → `m_token_cache_creation`（CTokenItem，Cache Write）
- `2` → `m_token_cache_read`（CTokenItem，Cache Read）
- `3` → `m_token_out`（CTokenItem，Output）

`index < 0` 或 `index >= 4` → 返回 `nullptr`。主程序在 `PluginManager.cpp:112-121` 用 `while (GetItem(i))` 累加所有 item，遇到 `nullptr` 停。

### `DataRequired()`（line 181）

主程序 1Hz 调用（`TrafficMonitorDlg.cpp:1504`）。本插件实现：`DataManager::Instance().Tick()`。

### `ShowOptionsDialog(void* hParent)`（line 198）

返回 `OR_OPTION_CHANGED` / `OR_OPTION_UNCHANGED` / `OR_OPTION_NOT_PROVIDED` 三选一。本插件实现：弹 `COptionsDlg`，用户点 OK 才返回 `OR_OPTION_CHANGED`，否则 `OR_OPTION_UNCHANGED`。

### `GetInfo(PluginInfoIndex)`（line 215）

按 index 返回插件元信息字符串。本插件返回：
- `TMI_NAME` → "ClaudeTokenMonitor"
- `TMI_DESCRIPTION` → "Monitors Claude Code token consumption in real-time"
- `TMI_AUTHOR` → "youzhiyang"
- `TMI_COPYRIGHT` → "Copyright (C) 2026"
- `TMI_VERSION` → "1.0"
- `TMI_URL` → "https://github.com/youzhiyang/TrafficMonitor"

### `OnExtenedInfo(ExtendedInfoIndex, const wchar_t*)`（line 276）

主程序在 `PluginManager.cpp:91-94` 把 `EI_CONFIG_DIR` 传给插件（用 `version >= 2` 闸）。本插件实现：收到 `EI_CONFIG_DIR` 时把路径存到 `DataManager::Instance().m_config_dir`，下一帧 `LoadConfig()` 从此目录读 `<config_dir>\ClaudeTokenMonitor.dll.ini`。

### `OnInitialize(ITrafficMonitor* pApp)`（line 323）

主程序在 `PluginManager.cpp:98-101` 把 `&theApp` 传给插件（用 `version >= 7` 闸）。本插件实现：保存 `pApp` 到成员变量，后续通过 `pApp->GetStringRes()` / `pApp->GetMonitorValue()` / `pApp->ShowNotifyMessage()` 调用反向接口。

## 必须导出的函数

`ITMPlugin* TMPluginGetInstance()`（line 428-430）

主程序在 `PluginManager.cpp:70` 用 `GetProcAddress` 找这个符号。返回的 `ITMPlugin*` 必须是全局或静态对象，**进程结束前不能被释放**（line 430 显式约束）。本插件实现：

```cpp
static CClaudeTokenMonitorPlugin s_instance;
ITMPlugin* TMPluginGetInstance() {
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &s_instance;
}
```

静态实例保证生命周期覆盖整个 DLL 加载期。

## GetItemId 稳定性

`IPluginItem::GetItemId()`（line 22）。返回值是"用户偏好 / 配置 / 选项里识别这个 item 的唯一 key"。

本插件 4 个 item 的 ID（**跨版本不能改**）：
- Token In：`"CTM_TokenIn_v1"`
- Cache Write：`"CTM_TokenCacheWrite_v1"`
- Cache Read：`"CTM_TokenCacheRead_v1"`
- Token Out：`"CTM_TokenOut_v1"`

后缀 `_v1` 是预留版本位：未来如果改这个 item 的语义（比如换数据源），用 `_v2` 新建一个 item，**保留旧 item 不动**，让用户的旧配置能继续读。

参考：PluginDemo 已有的 item ID 形式 `"4Tc21hGS"`（CustomDrawItem.cpp:12）、`"uQI0sH6a"`（PluginSystemDate.cpp:17）、`"B3tkxi5d"`（PluginSystemTime.cpp:17），都是随机串。本插件用语义化 ID 是因为同时有 4 个同类 item 需要在配置里被区分。

## IPluginItem 必须实现的虚函数

每个 `CTokenItem` 必须 override：

### 文本（line 16, 22, 28, 36, 43）

`IsCustomDraw()=true` 时，主程序只读 `GetItemName`（line 16）和 `GetItemId`（line 22），不读其他三个文本函数（line 50 注释显式说明）。本插件把 `GetItemLableText` / `GetItemValueText` / `GetItemValueSampleText` 都返回 `L""`。

### `IsCustomDraw() const`（line 53）

返回 `true`。必须自绘滚动柱形图（详见 `custom-draw.md`）。

### `GetItemWidth() const`（line 64）

返回 `40`（像素 @ 96 DPI，主程序按 DPI 自动放大）。这是最小宽度，`DrawItem` 收到的 `w` 可能比这大。

### `DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)`（line 76）

自绘实现。详见 `custom-draw.md`。

### `OnMouseEvent(MouseEventType, int, int, void*, int)`（line 115）

返回 `0` 让主程序处理（弹出右键菜单等）。本插件不拦截鼠标事件。

### `IsDrawResourceUsageGraph() const`（line 145）

返回 `0`（不显示主程序自带的状态条图）。`IsCustomDraw=true` 时此值被忽略，但显式返回 `0` 避免误触发 `TaskBarDlg.cpp:391` 的状态条路径。

## API 版本要求

主程序对本插件调用的所有接口都有最低版本要求：

| 接口 | 最低主程序 API 版本 | 引用 |
|---|---|---|
| `GetTooltipInfo` | 2 | `PluginInterface.h:442` |
| `GetItemWidthEx` / `OnMouseEvent` | 3 | `PluginInterface.h:444` |
| `OnKeboardEvent` / `OnItemInfo` | 4 | `PluginInterface.h:446` |
| `GetCommand*` / `OnPluginCommand` | 5 | `PluginInterface.h:448` |
| `GetResourceUsageGraphType/Value` | 6 | `PluginInterface.h:450` |
| `OnInitialize` | 7 | `PluginInterface.h:452` |

本插件实现用到的接口：
- `OnInitialize`（line 323）→ 需要主程序 API 版本 >= 7
- `OnExtenedInfo(EI_CONFIG_DIR, ...)`（line 276）→ 需要主程序 API 版本 >= 2
- `OnExtenedInfo(EI_DRAW_TASKBAR_WND, ...)` 在主程序侧在 `TaskBarDlg.cpp:422-425` 用 `version >= 2` 闸
- `DrawItem`（line 76）→ 无版本要求（属于 `IPluginItem`，从 v1 起就有）

主程序支持的最低 API 版本是 `PLUGIN_UNSUPPORT_VERSION`，在 `PluginManager.cpp:83` 做闸。本插件 `GetAPIVersion()` 返回 `7`，满足所有调用路径。

## MFC 状态切换

DLL 形式的 MFC 扩展需要 `AFX_MANAGE_STATE(AfxGetStaticModuleState())` 在每个对外 entry 入口切换 module state。`PluginDemo.cpp:51, 78, 114` 三处显式调用。本插件照搬这三处：

- `TMPluginGetInstance`（导出函数）：MFC 必须
- `GetInfo`：因为返回 `CString`，MFC 必须
- `ShowOptionsDialog`：弹 `COptionsDlg`（MFC 对话框），MFC 必须

非 MFC 入口（`GetItem` / `DataRequired` / `OnExtenedInfo` / `OnInitialize`）不切 module state，因为它们只返回 `IPluginItem*` / 调 `DataManager::Instance()` / 存指针，不触 MFC 资源。