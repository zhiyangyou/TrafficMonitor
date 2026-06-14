# 02 · 架构与模块拆分

> 范围：本项目（ClaudeTokenMonitor 插件）内部的 10 个 C++ 类职责、依赖图、全局对象。所有"实现"指计划文件 §2 模块拆分里登记的设计状态。

## 2.1 模块拆分（10 个 C++ 类）

| 类 | 文件（计划） | 职责 | 生命周期 |
| --- | --- | --- | --- |
| `CClaudeTokenMonitorPlugin` | `ClaudeTokenMonitor.cpp/h` | `ITMPlugin` 实现；持有 4 个 `CTokenItem`、`CDataManager*`、`CStatuslineInstaller*`；`OnInitialize` 保存 `ITrafficMonitor*`；`TMPluginGetInstance()` 导出此单例 | 全局静态对象，进程结束前不释放 |
| `CDataManager` | `DataManager.cpp/h` | 单例；持有 `CPerSessionAccumulator` map、4 个 `CRingBuffer`、`SettingData`、`m_string_table`；`LoadConfig`/`SaveConfig`/`Tick()`；格式化 value text 缓存 | 单例 |
| `CPerSessionAccumulator` | `Accumulator.cpp/h` | 每 session 维护 `SessionState`；`OnStatuslineUpdate()` 返回 `Delta`；`ForgetInactiveSessions(now_ms, 600000)` | `CDataManager` 值成员 |
| `CRingBuffer<float, N>` | `RingBuffer.h/cpp` | 固定容量环形缓冲；`std::array<T,N>` + 头/尾指针；4 个 item 各 1 个（归一化后 0~1 的浮点） | `CDataManager` 值成员 |
| `CSidecarReader` | `SidecarReader.cpp/h` | `CreateFile` 共享读 sidecar，按 EOF 偏移增量解析 JSONL；`DrainNewEntries()` 返回 `vector<StatuslineEntry>`；`ResetOffset()` | `CDataManager` 值成员 |
| `CStatuslineInstaller` | `StatuslineInstaller.cpp/h` | `CheckInstalled()` / `Install()` / `Uninstall()`；读写 `~/.claude/settings.json`（备份到 `%APPDATA%\ClaudeTokenMonitor\settings.json.bak.<unix-ts>`，记下原 `statusLine.command` 到 `previous-statusline.txt`） | `CClaudeTokenMonitorPlugin` 拥有的指针 |
| `CTokenItem` | `TokenItem.cpp/h` | 4 个 item 共享逻辑：`GetItemId/GetItemName/GetItemLableText/GetItemValueText/IsCustomDraw/DrawItem/GetItemWidth*`；具体数值类型与颜色由构造参数定；`IsCustomDraw=true`，`DrawItem` 画滚动柱形图 | `CClaudeTokenMonitorPlugin` 拥有的值成员 |
| `COptionsDlg` | `OptionsDlg.cpp/h` | MFC 对话框：状态条 / Install / Uninstall / 汇总模式下拉 / 单 session 下拉 / ignored 列表 / 4 颜色按钮 / 刷新频率 spin / OK / Cancel | 局部栈对象（`ShowOptionsDialog` 内构造） |
| `CPluginDemo` 复用参考 | `PluginDemo/PluginDemo.cpp` | 主程序 PluginDemo 项目提供的单例模式、`TMPluginGetInstance` 导出模式、Singleton + config 持久化范例、StringRes 调用模式——**不是本项目类**，只作为实现参照 | 参考 |
| `CIniHelper` 复用参考 | `PluginDemo/CommonData.h` 或 `TrafficMonitor/IniHelper.h` | INI 读写封装，本项目 `CDataManager::LoadConfig/SaveConfig` 直接复用——**不是本项目类**，只作为实现参照 | 参考 |

## 2.2 类之间的依赖图

```
                  ┌──────────────────────────────────┐
                  │  CClaudeTokenMonitorPlugin       │  (ITMPlugin 实现, 单例)
                  │   - 持有 4× CTokenItem           │
                  │   - 拥有 CStatuslineInstaller*   │
                  │   - 保存 ITrafficMonitor*        │
                  └─────────────┬────────────────────┘
                                │ 调用
                                ▼
                  ┌──────────────────────────────────┐
                  │  CDataManager (单例)              │
                  │   - 持有 CPerSessionAccumulator   │
                  │   - 持有 4× CRingBuffer          │
                  │   - 持有 CSidecarReader          │
                  │   - 持有 SettingData             │
                  │   - Tick() 主循环                │
                  └─────────────┬────────────────────┘
                                │ 内部组合
              ┌─────────────────┼──────────────────┐
              ▼                 ▼                  ▼
    ┌─────────────────┐ ┌──────────────────┐ ┌──────────────────┐
    │CPerSession      │ │CSidecarReader    │ │CRingBuffer × 4   │
    │Accumulator      │ │(DrainNewEntries) │ │(归一化 0~1 浮点) │
    │(OnStatusline    │ └──────────────────┘ └──────────────────┘
    │ Update→Delta)   │
    └─────────────────┘
              ▲
              │ 输入
              │
    ┌─────────────────┐
    │CTokenItem × 4   │── DrawItem 时读 CDataManager 的环形缓冲 + value text
    └─────────────────┘

    CStatuslineInstaller  ←─  CClaudeTokenMonitorPlugin 调用 Install/Uninstall
                                写 ~/.claude/settings.json + %APPDATA%\ClaudeTokenMonitor\
```

- 依赖方向严格自上而下：`Plugin` → `DataManager` → (`Accumulator` / `SidecarReader` / `RingBuffer`)
- `CTokenItem` 是叶子节点，只读 `CDataManager` 内部状态（环形缓冲 + value text 缓存）
- `CStatuslineInstaller` 是独立子树，由 Plugin 拥有但不参与数据流
- 4 个 `CTokenItem` 之间互不通信

## 2.3 全局对象

| 对象 | 类型 | 位置 | 作用 |
| --- | --- | --- | --- |
| `CDataManager::Instance()` | `CDataManager` 单例 | 计划中 `DataManager.cpp` | 配置读写、4 路环形缓冲、`CPerSessionAccumulator`、`CSidecarReader` 的统一宿主；`Tick()` 是 1Hz 数据流心脏 |
| `CClaudeTokenMonitorPlugin::m_instance` | `CClaudeTokenMonitorPlugin` 静态成员 | 计划中 `ClaudeTokenMonitor.cpp` | `ITMPlugin` 实现，导出函数 `TMPluginGetInstance()` 返回 `&m_instance`（参考 `PluginDemo/PluginDemo.cpp:6` 的 `CPluginDemo CPluginDemo::m_instance`） |
| `ITrafficMonitor* pApp` | `ITrafficMonitor*` | 插件 `OnInitialize` 注入（`TrafficMonitor/PluginManager.cpp:100`） | 插件可调 `GetPluginConfigDir()` 取配置目录、调 `ShowNotifyMessage()` 显示通知、调 `GetStringRes()` 取 i18n 文本（参考 `PluginDemo/PluginDemo.cpp:104-110`） |
| Sidecar 路径常量 | `std::wstring` | 计划 §4.4 写入路径 | `%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl` |
| Wrapper 路径常量 | `std::wstring` | 计划 §4.4 写入路径 | `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1`（可被 `SettingData::wrapper_path_override` 覆盖） |
| Settings 备份路径 | `std::wstring` | 计划 §4.4 写入路径 | `%APPDATA%\ClaudeTokenMonitor\settings.json.bak.<unix-ts>`（多次装只保留最新） |
| 原 statusline 备份 | `std::wstring` | 计划 §4.4 写入路径 | `%APPDATA%\ClaudeTokenMonitor\previous-statusline.txt`（Wrapper 找回原命令用） |
| 插件配置目录 | `std::wstring` | `OnExtenedInfo(EI_CONFIG_DIR, ...)` 传入（`PluginManager.cpp:94`） | `<exe_dir>\config\plugins\` 或 `%APPDATA%\TrafficMonitor\config\plugins\`；`CDataManager::LoadConfig` 在此目录下读 `ClaudeTokenMonitor.dll.ini` |

## 2.4 不变量与边界

- **数据流单入口**：`CDataManager::Tick()` 是 1Hz 数据流的唯一入口；其它组件都不主动触发数据更新。
- **绘制单出口**：4 个 `CTokenItem::DrawItem` 是 4 路柱形图的唯一绘制入口；主程序通过 `IsCustomDraw=true` 分支（`TrafficMonitor/TaskBarDlg.cpp:426-457`）调它们。
- **配置单写入**：`COptionsDlg` 关闭时（`IDOK`）把 `dlg.m_data` 整体赋回 `CDataManager::Instance().m_setting_data`（参考 `PluginDemo/PluginDemo.cpp:81-86` 的 `dlg.DoModal() == IDOK` 模式）；其它位置不直接改 `SettingData`。
- **进程内单实例**：`CDataManager` 与 `CClaudeTokenMonitorPlugin` 都是单例；主程序 `FreeLibrary` + `LoadLibrary` 插件时 `OnInitialize` 触发 `CDataManager::Instance().LoadConfig()` + `m_acc.Reset()` + `m_reader.ResetOffset()`（计划 §7 风险表）防止脏 delta。
- **跨进程文件**：`sidecar.jsonl` 是唯一跨进程边界——Wrapper（Claude Code 子进程）写，本插件（TrafficMonitor 进程）以共享读模式读。

## 2.5 与本节相关的关键文件索引

- 模块拆分 + 类职责：主计划文件 §2 模块拆分
- `ITMPlugin` / `IPluginItem` 契约：`include/PluginInterface.h`
- 主程序加载插件的入口：`TrafficMonitor/PluginManager.cpp:35-135`
- 主程序绘制分发的关键路径：`TrafficMonitor/TaskBarDlg.cpp:381-485`
- PluginDemo 单例 + `TMPluginGetInstance` 范例：`PluginDemo/PluginDemo.cpp:6,12-15,112-116`
- `CDataManager` INI 持久化模式参考：`PluginDemo/DataManager.cpp:27-35`
