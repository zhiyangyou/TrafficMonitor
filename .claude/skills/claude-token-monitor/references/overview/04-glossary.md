# 04 · 补充术语 / 辅助解释

> 范围：01-context.md 没收录的辅助解释。包括主程序与本插件交互面、主程序内部宏/枚举、wrapper 脚本中用到的字段名约定。本表只解释"是什么"，不解释"为什么这样选"。

## A. 主程序内部概念（与本插件相关但不在 01-context.md 收录范围）

### TASKBAR_GRAPH_STEP

- **是什么**：`CTaskBarDlg` 内部的"平均窗口"——`AddHisToList`（`TrafficMonitor/TaskBarDlg.cpp:1350-1375`）每收到 `TASKBAR_GRAPH_STEP` 个原始数据才把链表头除以 `TASKBAR_GRAPH_STEP` 求一次平均；主程序用此机制把高频抖动平滑成 1 个值。
- **本插件是否使用**：否。本插件的 4 个 `CTokenItem` 自己用 `CRingBuffer<float, N>` 维护归一化后的 0~1 浮点序列，不走 `TASKBAR_GRAPH_STEP` 路径——主程序这个宏只对内置项的 `m_map_history_data[CommonDisplayItem]` 生效。
- **关键行号**：`TrafficMonitor/TaskBarDlg.cpp:1350, 1355, 1359`。

### CommonDisplayItem 枚举

- **是什么**：主程序内置显示项枚举（CPU / 内存 / 上传 / 下载 / 温度 / GPU / ...），存于 `CommonData.h`。
- **本插件的关联**：插件的 4 个 `IPluginItem` 不进 `CommonDisplayItem` 枚举，而是由主程序通过 `ITMPlugin::GetItem(int)` 收集后追加到 `m_plugins` 列表（`TrafficMonitor/PluginManager.cpp:118`），在 `m_all_display_items_with_plugins` 中与内置项并列（`PluginManager.cpp:127-134`）。
- **副作用**：`TaskBarDlg.cpp:397-398` 调 `AddHisToList(item, figure_value)` 与 `TryDrawGraph(drawer, rect, item)` 时，参数 `item` 实际类型是 `IPluginItem*` 而函数声明要求 `CommonDisplayItem`——这导致**该代码路径在编译期对插件是坏的**（计划文件 §关键架构发现 详述）。本插件**必须**走 `IsCustomDraw=true` 自绘路径，不依赖 `AddHisToList` / `TryDrawGraph`。

### PluginState 枚举

- **是什么**：插件加载状态枚举，存于 `PluginManager.h`。值：
  - `PS_SUCCEED` — 加载成功
  - `PS_MUDULE_LOAD_FAILED` — `LoadLibrary` 失败（**注意源码 `MUDULE` 拼写保留**）
  - `PS_FUNCTION_GET_FAILED` — `GetProcAddress("TMPluginGetInstance")` 失败
  - `PS_VERSION_NOT_SUPPORT` — `plugin->GetAPIVersion() <= PLUGIN_UNSUPPORT_VERSION`
  - `PS_DISABLE` — 命中 `MainConfigData::plugin_disabled` 黑名单
- **来源**：`TrafficMonitor/PluginManager.cpp:56, 64, 73, 85`。

### PluginInfoIndex 枚举（TMI_*）

- **是什么**：`ITMPlugin::PluginInfoIndex`，主程序拉取插件元信息的 6 项索引。值：
  - `TMI_NAME = 0` — 插件显示名
  - `TMI_DESCRIPTION = 1` — 插件描述
  - `TMI_AUTHOR = 2` — 作者
  - `TMI_COPYRIGHT = 3` — 版权
  - `TMI_VERSION = 4` — 版本号
  - `TMI_URL = 5` — 主页 URL
  - `TMI_MAX = 6` — 哨兵值
- **来源**：`include/PluginInterface.h:201-210`。
- **加载期调用**：`TrafficMonitor/PluginManager.cpp:104-108` 循环 `i < TMI_MAX` 把 6 项 `wchar_t*` 拷到 `plugin_info.properties[index]`。

### ExtendedInfoIndex 枚举（EI_*）

- **是什么**：`ITMPlugin::ExtendedInfoIndex`，主程序→插件单向推送通道的索引。当前覆盖：
  - 颜色相关：`EI_LABEL_TEXT_COLOR`、`EI_VALUE_TEXT_COLOR`（推送 `std::to_wstring(color).c_str()`）
  - 绘制上下文：`EI_DRAW_TASKBAR_WND`（`L"1"`=任务栏窗口 / `L"0"`=皮肤）
  - 主窗口选项：`EI_MAIN_WND_NET_SPEED_SHORT_MODE`、`EI_MAIN_WND_SPERATE_WITH_SPACE`、`EI_MAIN_WND_UNIT_BYTE`、`EI_MAIN_WND_UNIT_SELECT`、`EI_MAIN_WND_NOT_SHOW_UNIT`、`EI_MAIN_WND_NOT_SHOW_PERCENT`
  - 任务栏窗口选项：`EI_TASKBAR_WND_NET_SPEED_SHORT_MODE`、`EI_TASKBAR_WND_SPERATE_WITH_SPACE`、`EI_TASKBAR_WND_VALUE_RIGHT_ALIGN`、`EI_TASKBAR_WND_NET_SPEED_WIDTH`、`EI_TASKBAR_WND_UNIT_BYTE`、`EI_TASKBAR_WND_UNIT_SELECT`、`EI_TASKBAR_WND_NOT_SHOW_UNIT`、`EI_TASKBAR_WND_NOT_SHOW_PERCENT`
  - 配置目录：`EI_CONFIG_DIR`（推送 `std::wstring`，即 `<config_dir>\plugins\`）
- **来源**：`include/PluginInterface.h:243-268`。
- **本插件的接收方**：`CClaudeTokenMonitorPlugin::OnExtenedInfo(index, data)` 中只关心 `EI_CONFIG_DIR`，把 `data` 存为 `m_config_dir` 用于 `CDataManager::LoadConfig/SaveConfig`（参考 `PluginDemo/PluginDemo.cpp:90-102`）。

### Plugin API version

- **是什么**：`ITMPlugin::GetAPIVersion()` 当前返回值 `7`，存于 `include/PluginInterface.h:166`。
- **变更日志**（`include/PluginInterface.h:434-454`）：
  - v1 — 第一个版本
  - v2 — 新增 `ITMPlugin::GetTooltipInfo`
  - v3 — 新增 `IPluginItem::GetItemWidthEx`、`IPluginItem::OnMouseEvent`
  - v4 — 新增 `IPluginItem::OnKeboardEvent`、`IPluginItem::OnItemInfo`
  - v5 — 新增 `ITMPlugin::GetCommandName`、`GetCommandIcon`、`OnPluginCommand`
  - v6 — 新增 `IPluginItem::GetResourceUsageGraphType`、`GetResourceUsageGraphValue`
  - v7 — 新增 `ITMPlugin::OnInitialize`
- **`PLUGIN_UNSUPPORT_VERSION`**：插件管理器对 v 之前的版本判定为不支持（`PluginManager.cpp:83`）。当前实现下 v1 即不被支持。

### MonitorInfo 结构

- **是什么**：主程序在每次 `DoMonitorAcquisition` 末尾推给每个插件的 11 字段快照。定义见 `include/PluginInterface.h:218-231`：
  - `up_speed`、`down_speed`（`unsigned long long`）
  - `cpu_usage`、`memory_usage`、`gpu_usage`、`hdd_usage`（`int`）
  - `cpu_temperature`、`gpu_temperature`、`hdd_temperature`、`main_board_temperature`（`int`，摄氏度）
  - `cpu_freq`（`int`）
- **本插件是否使用**：否。`CClaudeTokenMonitorPlugin::OnMonitorInfo` 重写为空（计划 §3.1 + §3.2 模块拆分），本插件不读主程序网速/CPU/温度等指标。
- **注意**：接口注释（`include/PluginInterface.h:362-369`）已标"将弃用"——`ITrafficMonitor::GetMonitorValue` 是新版通道。

### OptionReturn 枚举

- **是什么**：`ITMPlugin::OptionReturn`，`ShowOptionsDialog` 返回值：
  - `OR_OPTION_CHANGED = 0` — 用户在对话框里改了设置
  - `OR_OPTION_UNCHANGED = 1` — 用户取消/未改
  - `OR_OPTION_NOT_PROVIDED = 2` — 插件不提供选项对话框
- **来源**：`include/PluginInterface.h:184-189`。
- **本插件**：`ShowOptionsDialog` 走 `dlg.DoModal() == IDOK` 分支返回 `OR_OPTION_CHANGED`（参考 `PluginDemo/PluginDemo.cpp:76-87`）。

## B. wrapper 脚本相关字段

### originalCommand

- **是什么**：`~/.claude/settings.json` 里 `statusLine` 节点下保存的"原 statusline 命令"字段。`CStatuslineInstaller::Install()` 把用户原 `statusLine.command` 值搬到 `statusLine.originalCommand`，再把 `statusLine.command` 覆盖为 `powershell -ExecutionPolicy Bypass -File <wrapper>` 路径。
- **Wrapper 读取**：Wrapper 脚本用 `(Get-Content $settings -Raw | ConvertFrom-Json).statusLine.originalCommand` 找回原命令并 pipe stdin 给它。
- **首次安装无 statusline**：`originalCommand` 字段不存在，Wrapper 静默跳过 pipe（不报错，不写日志）。
- **卸载时**：`Uninstall()` 读 `previous-statusline.txt`（不是 `originalCommand`，因为 `settings.json` 已被覆盖为 wrapper 自己的状态）—— 还原为 `previous-statusline.txt` 的内容；若该文件不存在则删除 `statusLine` 整个 key。

### wrapper_ms

- **是什么**：Wrapper 在原始 JSON 末尾追加的 Unix 毫秒时间戳字段（`[int64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())`）。
- **目的**：plugin 端 `CSidecarReader` 拿 `wrapper_ms` 做新鲜度检查（> 5 分钟丢弃），但不参与 `Delta` 计算——`Delta` 用 `GetTickCount64()` 本进程时间，避免跨进程时间漂移。
- **位置**：每行 JSONL 末尾，紧跟在 `}` 闭合之前。

## C. 项目内部命名约定

| 约定 | 例子 | 含义 |
| --- | --- | --- |
| `m_*` | `m_acc`、`m_reader`、`m_window` | 类的私有/受保护成员变量 |
| `I` 前缀 | `IPluginItem`、`ITMPlugin`、`ITrafficMonitor` | 纯虚接口类 |
| `C` 前缀 | `CDataManager`、`CPerSessionAccumulator`、`CStatuslineInstaller` | 具体类（非接口） |
| 静态单例 | `CDataManager::Instance()` | 标准 Meyers singleton 模式 |
| `Statusline` 一词 | `StatuslineEntry`、`statusline-wrapper.ps1`、`CStatuslineInstaller` | 一律指 Claude Code 传给 `~/.claude/settings.json::statusLine.command` 的 JSON payload 与拦截脚本；**不**用于描述本插件的 JSONL sidecar（那个用 "sidecar"） |

## D. 待澄清

- （暂无）
