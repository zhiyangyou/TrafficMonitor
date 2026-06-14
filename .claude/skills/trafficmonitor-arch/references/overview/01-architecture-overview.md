# 01 · 解决方案架构总览

> 范围：3 个项目之间的依赖、TrafficMonitor 主程序的子系统和关键单例/全局对象。
> 本文只描述"是什么、谁拥有谁、谁被谁调用"，不展开实现细节。

## 2.1 解决方案的 3 个项目及其依赖关系

```
                    TrafficMonitor.sln
   ┌──────────────────────────────────────────────────────┐
   │                                                      │
   │   TrafficMonitor.vcxproj  ──depends on──►  OpenHardwareMonitorApi.vcxproj
   │   (MFC Application)                              (C++/CLI DLL)
   │        │                                              │
   │        │  links                                       │
   │        ▼                                              │
   │   OpenHardwareMonitorApi.lib ◄─────── 产出           │
   │   PluginInterface.h ◄────────────  include ../include/PluginInterface.h
   │                                                      │
   │   PluginDemo.vcxproj  ──独立──► PluginDemo.dll        │
   │   (MFC DLL, demo)            （仅 PluginDemo 自己演示如何写插件）     │
   │   ─────────────────────────────────────────────────────
   │   TrafficMonitor_Lite.sln 省略 OpenHardwareMonitorApi，通过 WITHOUT_TEMPERATURE 宏关闭温度相关代码路径
   └──────────────────────────────────────────────────────┘
```

- `TrafficMonitor.sln:6-15` 把 3 个项目并列在同一个 sln；`TrafficMonitor.sln:7-9` 显式 PostProject 声明 `TrafficMonitor` 依赖 `OpenHardwareMonitorApi`。
- `TrafficMonitor/TrafficMonitor.vcxproj:773` 通过 `<ClInclude Include="..\include\PluginInterface.h" />` 把插件接口头加入主项目，使 `TrafficMonitor.h:24` 引用 `ITMPlugin`/`IPluginItem`/`ITrafficMonitor`。
- `OpenHardwareMonitorApi/OpenHardwareMonitorApi.vcxproj` 是一份 C++/CLI 包装，编译为 `OpenHardwareMonitorApi.dll`（同目录还放置 `LibreHardwareMonitorLib.dll`）。`OpenHardwareMonitorApi/OpenHardwareMonitorImp.h:14-64` 用 `COpenHardwareMonitor` 实现头文件 `include/OpenHardwareMonitor/OpenHardwareMonitorApi.h` 中的抽象接口。
- `PluginDemo/PluginDemo.vcxproj` 编译为 `PluginDemo.dll`，演示一个最小可用的 `ITMPlugin` 实现（`PluginDemo/PluginDemo.h:7-30`，导出 `TMPluginGetInstance` 见 `PluginDemo.h:35`）。**PluginDemo 不被 TrafficMonitor 引用**——它只是给插件作者看的样例。
- Lite sln（`TrafficMonitor_Lite.sln:1-9`）移除 `OpenHardwareMonitorApi` 项目，TrafficMonitor 在 Lite 配置下不链接 `OpenHardwareMonitorApi.lib`（`TrafficMonitor.vcxproj:329,446,484,570,698,740` 全部无 `OpenHardwareMonitorApi.lib`）。

## 2.2 TrafficMonitor 主程序的子系统切分

主程序源码位于 `TrafficMonitor/`，按职责可以切分为下列"层/子系统"。每层都引用下面更低层，不反向依赖。

### 2.2.1 窗口层（顶层对话/窗体）
- `CTrafficMonitorDlg`（`TrafficMonitor/TrafficMonitorDlg.h:30-285`）—— 主对话框，承载皮肤、菜单、托盘图标、设置入口，并负责启动任务栏窗口子对象。
- `CTaskBarDlg`（`TrafficMonitor/TaskBarDlg.h:19-234`）—— 任务栏窗口的基类，定义所有任务栏相关抽象行为（`InitTaskbarWnd`、`AdjustTaskbarWndPos` 等，见 `TaskBarDlg.h:84-88`）。
- `CClassicalTaskbarDlg` / `CWin11TaskbarDlg` / `CWineTaskbarDlg`（`ClassicalTaskbarDlg.h`、`Win11TaskbarDlg.h`、`WineTaskbarDlg.h`）—— 三个 `CTaskBarDlg` 的具体派生，分别对应经典任务栏、Win11 任务栏、Wine 环境。
- 各种次级对话框：`CSkinDlg`、`COptionsDlg`、`CGeneralSettingsDlg`、`CTaskBarSettingsDlg`、`CMainWndSettingsDlg`、`CNetworkInfoDlg`、`CAboutDlg`、`CHistoryTrafficDlg` 等。

### 2.2.2 UI 控件层
- 封装 MFC 标准控件的扩展：`CStaticEx`、`CColorStatic`、`CPictureStatic`、`CLinkStatic`、`CListCtrlEx`、`CColorSettingListCtrl`、`CCTabCtrlEx`、`CSpinEdit`、`CComboBox2`、`CFileDialogEx`。
- 通用基类：`CBaseDialog`（被多数设置对话框继承，`BaseDialog.h`）。
- 通知区辅助：`NOTIFYICONDATA m_ntIcon`（`TrafficMonitorDlg.h:57`），由 `AddNotifyIcon`/`DeleteNotifyIcon` 维护。

### 2.2.3 绘制层
- 接口：`IDrawCommon`（`TrafficMonitor/IDrawCommon.h`）、`IDrawBuffer`。
- GDI 实现：`CDrawCommon`（`DrawCommon.h:8-61`）+ `CDrawDoubleBuffer`（`DrawCommon.h:65-101`）。
- D2D/D3D10/DComposition 实现：`CTaskBarDlgDrawCommon` + `CTaskBarDlgDrawBuffer` / `CTaskBarDlgDrawBufferUseDComposition`（`TaskBarDlgDrawCommon.h:758-875`），依赖 `CTaskBarDlgDrawCommonSupport` 与 `CD2D1DeviceContextWindowSupport`。
- 工厂：`DrawCommonFactory.h`（`DrawCommonFactory.h:25-32` 的 `DrawCommonUnionStorage`、`DrawBufferUnionStorage`）—— 提供栈内存 union，按 `RenderType` 选择具体实现。
- 渲染管线枚举：`SupportedRenderEnums.h`。
- 文本绘制钩子：`DrawTextManager.h` + `TaskBarDlgDrawCommon.h:480-660` 的 `TaskBarDlgUser32DrawTextHook`——Hook `User32!DrawText{A,W,ExA,ExW}`，把插件和 MFC 的文本输出路由到自定义 DC，从而获得亚像素对齐（`PluginManager.cpp:410-474` 完成 IAT 改写）。

### 2.2.4 数据采集层
- 流量：`TrafficMonitorDlg.cpp:1164-1241` 的 `CTrafficMonitorDlg::DoMonitorAcquisition`，调用 `GetIfTable`（Iphlpapi）后比较 `m_in_bytes/m_out_bytes` 与 `m_last_in_bytes/m_last_out_bytes` 计算 `m_in_speed/m_out_speed`。
- CPU / GPU / 硬盘 / 频率：`PdhHardwareQuery/` 子目录的 4 个 wrapper（`CPUUsage.h`、`GpuUsage.h`、`DiskUsage.h`、`CpuFreq.h`），基类 `CPdhQuery`（`PdhQuery.h:5-26`）。
- 温度/占用/频率/HDD：`OpenHardwareMonitorApi` 提供的 `m_pMonitor`（`TrafficMonitor.h:103-104`）——通过 `m_minitor_lib_critical` 临界区保护（`TrafficMonitor.h:106`），在 `TrafficMonitor.cpp:621-634` 的 `InitOpenHardwareMonitorLibThreadFunc` 中后台初始化。
- 内存：`GlobalMemoryStatusEx`（`TrafficMonitorDlg.cpp:1405`）。
- 历史流量：`CHistoryTrafficFile`（`HistoryTrafficFile.h:3-30`）—— 二进制文件存于 `m_history_traffic_path`（`TrafficMonitor.cpp:956`）。

### 2.2.5 设置/配置层
- INI 读写：`CIniHelper`（`IniHelper.h:7-58`）—— 主程序和插件都通过它操作 `config.ini`。
- 强类型设置结构：`CommonData.h:190-401` 定义 `MainConfigData`、`MainWndSettingData`、`TaskBarSettingData`、`GeneralSettingData`、`SkinSettingData`、`PublicSettingData`、`DispStrings`、`FontInfo`、`HardwareItem` 等。
- 高级读写辅助：`CSettingsHelper`（`SettingsHelper.h/.cpp`）封装 `LoadTaskbarWndColors` / `SaveDisplayStr` 等。
- 任务栏预设方案：`CTaskbarDefaultStyle`（`TaskbarDefaultStyle.h:8-35`）—— 4 套预设（`TASKBAR_DEFAULT_STYLE_NUM=4`），用于 Win10/11 深色-浅色模式切换。
- 全局配置：`CTrafficMonitorApp::LoadGlobalConfig` / `SaveGlobalConfig`（`TrafficMonitor.cpp:464-508`）—— `global_cfg.ini` 决定 portable_mode，影响 `m_config_dir`。
- 项目顺序：`CTaskbarItemOrderHelper`（`TaskbarItemOrderHelper.h:4-31`）—— 用户可拖拽重排任务栏显示项。

### 2.2.6 插件层
- 加载器：`CPluginManager`（`PluginManager.h:10-69`），遍历 `plugins\*.dll` → `LoadLibrary` → `GetProcAddress("TMPluginGetInstance")` → 拿到 `ITMPlugin*`（`PluginManager.cpp:35-135`）。
- 钩子改写：`CPluginManager::ReplacePluginDrawTextFunction`（`PluginManager.cpp:410-464`）解析 PE 导入表，将 `User32!DrawText{A,W,ExA,ExW}` 的 IAT 入口改写到主程序的 `User32DrawTextManager` 实现。
- 设置下发：`CTrafficMonitorApp::SendSettingsToPlugin`（`TrafficMonitor.cpp:1241-1264`）—— 在启动时把主窗口/任务栏窗口的呈现参数（短模式、单位、隐藏百分号等）通过 `ITMPlugin::OnExtenedInfo` 投递给每个插件。
- 菜单注入：`CTrafficMonitorApp::UpdatePluginMenu`（`TrafficMonitor.cpp:1266-1296`）把 `ITMPlugin::GetCommandCount` / `GetCommandName` 注入主菜单/任务栏菜单。
- 版本更新：`CPluginUpdateHelper`（`PluginUpdateHelper.h:24-34`）—— 在启动时通过 HTTP 拉取插件市场最新版本信息（`TrafficMonitor.cpp:613-617`）。

### 2.2.7 皮肤层
- 单个皮肤文件：`CSkinFile`（`SkinFile.h:7`）—— 用 tinyxml2 解析 `skins/<name>/skin.xml`，提取 `SkinInfo`（`SkinFile.h:22-39`）与每项布局。
- 皮肤管理：`CSkinManager`（`SkinManager.h:5-35`）—— 单例（`m_instance` 静态成员），负责枚举、加载、按皮肤保存 `SkinSettingData`。
- 路径：`m_skin_path`（`TrafficMonitor.cpp:946,952`）—— Debug 下硬编码 `.\skins\`，Release 下为 `<module_dir>\skins\`。
- 自适应：`MainConfigData::skin_auto_adapt` + `skin_name_light_mode/dark_mode`（`CommonData.h:207-209`）支持深浅色自动切肤。

### 2.2.8 本地化层
- `CStrTable`（`StrTable.h:7-46`）—— 单例（在 `TrafficMonitorApp` 中以值成员 `m_str_table` 持有，见 `TrafficMonitor.h:91`），从 `language\*.ini` 读取 key→string。
- `LanguageInfo`（`CommonData.h:171-187`）—— 含 BCP-47 代码、默认字体名。
- `CVariant`（`CVariant.h/.cpp`）—— `LoadTextFormat` 的占位符参数类型。
- 多语言文件：`language/English.ini`、`language/Simplified_Chinese.ini`、`language/Traditional_Chinese.ini`（`TrafficMonitor.vcxproj:1000-1002`），通过 `<None Include>` 加入部署清单。

### 2.2.9 工具与基础设施层
- 字符串/路径/时间：`CCommon`（`Common.h:7` 起 200+ 静态函数）。
- 文件版本：`CWinVersionHelper`（`WinVersionHelper.h:2-25`）—— 区分 Win7/8/8.1/10/11 + Wine。
- DPI：`CTrafficMonitorApp::DPI/...`（`TrafficMonitor.cpp:510-528,1312-1317`）+ `CTaskBarDlg` 自有的 `m_taskbar_dpi` 与 `DPI()`。
- 设置与系统集成：`auto_start_helper.h/.cpp`（注册表 + 任务计划两种自启方式）、`WindowsSettingHelper.h/.cpp`（深浅色模式判断）。
- 崩溃：`crashtool.h/.cpp`—— `CRASHREPORT::StartCrashReport()` 在 `CTrafficMonitorApp` 构造里调用（`TrafficMonitor.cpp:49`）。
- 更新：`CUpdateHelper`（`UpdateHelper.h/.cpp`）—— 多源（GitHub/Gitee）拉取版本信息，平台分支 x86/x64/ARM64EC。
- 平台特性检测：`WindowsWebExperienceDetector`（`WindowsWebExperienceDetector.h:3-7`）通过 WinRT API 检测 Web Experience Pack；`CTrafficMonitorApp::CheckWindows11Taskbar`（`TrafficMonitor.cpp:1298-1310`）通过 `FindWindowEx(Windows.UI.Composition.DesktopWindowContentBridge)` 判断 Win11 任务栏。
- 动态库按需加载：`CDllFunctions`（`TrafficMonitor.h:90`）—— 在运行时 `LoadLibrary`/`GetProcAddress` 取得 `GetDpiForMonitor` 等 API，避免硬依赖。

## 2.3 CTrafficMonitorApp 的角色

`CTrafficMonitorApp` 在 `TrafficMonitor.h:31-214` 的定义里同时继承两个完全不同的对象：

```
class CTrafficMonitorApp
    : public CWinApp                 // MFC 应用对象（消息循环、InitInstance/ExitInstance）
    , public ITrafficMonitor         // 暴露给插件的主程序接口
```

- **作为 CWinApp**：负责进程级生命周期。`TrafficMonitor.cpp:915-1131` 的 `InitInstance` 决定了：
  1. 改写对话框类名为 `APP_CLASS_NAME`（`TrafficMonitor.cpp:918-921`）。
  2. 计算 `m_module_dir/m_appdata_dir/m_config_path/...`（`TrafficMonitor.cpp:923-957`）。
  3. 载入全局配置 + 语言配置（`LoadGlobalConfig`、`LoadLanguageConfig`，`TrafficMonitor.cpp:464,59`）。
  4. 互斥量防多实例（`TrafficMonitor.cpp:981-1013`）。
  5. 载入插件 + 加载 INI 配置 + 任务栏默认样式（`TrafficMonitor.cpp:1015-1022`）。
  6. 启动时检查更新线程（`TrafficMonitor.cpp:1057-1061`）。
  7. `.NET 4.5` 检测 + 后台初始化 OpenHardwareMonitor（`TrafficMonitor.cpp:1063-1086`，受 `WITHOUT_TEMPERATURE` 守卫）。
  8. GDI+ 启动、`SendSettingsToPlugin`、创建并 `DoModal` 主对话框（`TrafficMonitor.cpp:1093-1101`）。
  9. `ExitInstance` 关闭 GDI+（`TrafficMonitor.cpp:1382-1388`）。
- **作为 ITrafficMonitor**：把以下方法以虚函数形式暴露给插件，参见 `TrafficMonitor.h:202-213` 与 `TrafficMonitor.cpp:1390-1497`：
  - `GetAPIVersion`、`GetVersion`（版本号）
  - `GetMonitorValue`、`GetMonitorValueString`（枚举 `MonitorItem` → 当前 `m_in_speed/m_cpu_usage/...`）
  - `GetStringRes(key, section)`（转发到 `m_str_table.LoadText`）
  - `ShowNotifyMessage`（调用主窗口的 `ShowNotifyTip` 显示一个通知）
  - `GetLanguageId`、`GetPluginConfigDir`、`GetDPI(DPI_MAIN_WND/DPI_TASKBAR)`
  - `GetThemeColor`（保存 `m_theme_color`，在 `CTrafficMonitorApp` 构造时由 `CCommon::GetWindowsThemeColor()` 初始化，`TrafficMonitor.cpp:56`）

由于 `ITrafficMonitor*` 在插件启动时通过 `ITMPlugin::OnInitialize` 注入（`PluginManager.cpp:98-101`），插件能直接调用主程序的 `GetMonitorValue` / `ShowNotifyMessage` 等接口。

## 2.4 关键单例与全局对象

| 对象 | 类型 | 位置（声明/定义） | 作用 |
| --- | --- | --- | --- |
| `theApp` | `CTrafficMonitorApp` | `TrafficMonitor.h:216`（`extern`），`TrafficMonitor.cpp:910`（定义） | 进程级全局，承载所有跨子系统共享状态 |
| `CTrafficMonitorApp::self` | `CTrafficMonitorApp*` | `TrafficMonitor.h:35`，`TrafficMonitor.cpp:37`（初始化为 `NULL`）、`TrafficMonitor.cpp:43`（构造时赋 `this`） | 在 CTrafficMonitorApp 构造完成前就能用的"早期 self"指针，避免 `theApp` 尚未初始化的时序问题 |
| `CIniHelper` 实例 | 局部对象 | `TrafficMonitor.cpp:61,67,289,460,473,492,979` 等 | 临时对象，作用域结束即释放——不持有长期状态 |
| `CSkinManager::m_instance` | `CSkinManager` 静态成员 | `SkinManager.h:33`（声明），`SkinManager.cpp`（定义） | 皮肤注册表，枚举所有可用皮肤并按名缓存 `SkinSettingData` |
| `CTrafficMonitorApp::m_plugins` | `CPluginManager` 值成员 | `TrafficMonitor.h:89`，初始化在 `CTrafficMonitorApp` 默认构造 | 唯一插件管理器；`TrafficMonitor.cpp:1017` 调 `m_plugins.LoadPlugins()` |
| `CTrafficMonitorApp::m_taskbar_default_style` | `CTaskbarDefaultStyle` 值成员 | `TrafficMonitor.h:88` | 任务栏深浅色预设方案 |
| `CTrafficMonitorApp::m_str_table` | `CStrTable` 值成员 | `TrafficMonitor.h:91` | 多语言字符串表（`m_str_table.Init()` 在 `TrafficMonitor.cpp:976`） |
| `CTrafficMonitorApp::m_plugin_update` | `CPluginUpdateHelper` 值成员 | `TrafficMonitor.h:92` | 启动时拉取插件市场版本 |
| `CTrafficMonitorApp::m_dll_functions` | `CDllFunctions` 值成员 | `TrafficMonitor.h:90` | 包装 `GetDpiForMonitor` 等按需 API |
| `CTrafficMonitorApp::m_d2d_taskbar_draw_common_support` | `CLazyConstructable<CTaskBarDlgDrawCommonSupport>` | `TrafficMonitor.h:108` | 任务栏 D2D 渲染所需的全局设备资源懒构造（首次访问 `Get()` 时才创建） |
| `CTrafficMonitorApp::m_pMonitor` | `shared_ptr<IOpenHardwareMonitor>` | `TrafficMonitor.h:103-104`，`WITHOUT_TEMPERATURE` 守卫 | 硬件监控后端的非托管代理 |
| `CTrafficMonitorApp::m_minitor_lib_critical` | `CCriticalSection` | `TrafficMonitor.h:106` | 保护 `m_pMonitor` 访问的临界区 |
| `CTrafficMonitorApp::m_win_version` | `CWinVersionHelper` 值成员 | `TrafficMonitor.h:84` | Windows 版本判断 |
| `CTrafficMonitorApp::m_notify_icons` | `HICON[MAX_NOTIFY_ICON]` | `TrafficMonitor.h:86` | 托盘图标资源句柄缓存 |
| `CTrafficMonitorApp::m_menu_icons` | `std::map<UINT, HICON>` | `TrafficMonitor.h:183` | 菜单图标资源句柄缓存（按 ID 缓存） |
| `CTrafficMonitorApp::m_main_menu` 等 6 个 `CMenu` | 值成员 | `TrafficMonitor.h:94-99` | 主窗口、任务栏窗口及其"插件区域"右键菜单模板 |
| `CTrafficMonitorApp::m_gdiplusToken` | `ULONG_PTR` | `TrafficMonitor.h:185` | GDI+ 启动 token，构造时分配、`ExitInstance` 释放 |
| `CTaskbarHelper` | 静态函数类 | `TaskbarHelper.h/.cpp` | 与任务栏窗口/Shell_TrayWnd 交互的辅助方法（无成员） |
| `CTrafficMonitorDlg::m_WM_TASKBARCREATED` | 静态 `unsigned int` | `TrafficMonitorDlg.h:115`，`TrafficMonitorDlg.cpp:31`（通过 `RegisterWindowMessage(L"TaskbarCreated")` 初始化） | 用于监听任务栏重建 |
| `g_d2d1_taskbar_draw_common_support` 等全局 D2D 设备 | 见 `TaskBarDlgDrawCommon.h:126-154` | 全局托管在 `theApp.m_d2d_taskbar_draw_common_support` 上 | D3D10/D2D1/DComposition 设备资源 |

> `CTrafficMonitorApp::self` 与 `theApp` 的关系：`theApp` 是 `extern` 全局对象（C++ 保证零初始化早于动态初始化），`self` 在 `CTrafficMonitorApp` 构造函数里被赋为 `this`（`TrafficMonitor.cpp:43`）。任何早于 `CTrafficMonitorApp` 构造运行的代码（如 `DllMain` 之前的 CRT 初始化）通过 `self` 安全访问。

## 2.5 入口与消息流骨架

```
[进程启动]
   └─► CWinApp::InitInstance (CTrafficMonitorApp 重写, TrafficMonitor.cpp:915)
         ├─► 注册窗口类
         ├─► 计算路径 / LoadGlobalConfig / LoadLanguageConfig
         ├─► InitInstance 进入对话框创建之前先 CreateMutex
         ├─► m_plugins.LoadPlugins()                       ← 加载 DLL 插件
         ├─► LoadConfig + m_taskbar_default_style.LoadConfig
         ├─► InitInstance 创建后台线程 InitOpenHardwareMonitorLibThreadFunc
         ├─► GdiplusStartup
         ├─► SendSettingsToPlugin                          ← 下发格式/单位
         └─► CTrafficMonitorDlg dlg; dlg.DoModal()
                └─► 消息循环：WM_TIMER(MONITOR_TIMER) 触发
                       m_monitor_data_required = true
                └─► MonitorThreadCallback(后台, TrafficMonitorDlg.cpp:1527)
                       └─► DoMonitorAcquisition() 写回 theApp.m_*
                              └─► SendMessage(WM_MONITOR_INFO_UPDATED) (TrafficMonitorDlg.cpp:1524)
                                      └─► 主对话框 + 任务栏窗口 重绘
```

线程协作的关键：`MONITOR_TIMER`（由主线程 `SetTimer` 启动，`TrafficMonitorDlg.cpp:1115`）只是把 `m_monitor_data_required` 置 true；真正的采集在 `MonitorThreadCallback` 中（`TrafficMonitorDlg.cpp:1527-1554`）。数据写回后通过 `SendMessage(WM_MONITOR_INFO_UPDATED)` 回到主线程，避免跨线程直接操作 MFC 控件。

## 2.6 与本节相关的关键文件索引

- 应用对象：`TrafficMonitor/TrafficMonitor.h`、`TrafficMonitor/TrafficMonitor.cpp`。
- 主对话框：`TrafficMonitor/TrafficMonitorDlg.h`、`TrafficMonitor/TrafficMonitorDlg.cpp`。
- 任务栏基类 + 三种派生：`TrafficMonitor/TaskBarDlg.h`、`TrafficMonitor/ClassicalTaskbarDlg.h`、`TrafficMonitor/Win11TaskbarDlg.h`、`TrafficMonitor/WineTaskbarDlg.h`。
- 设置结构：`TrafficMonitor/CommonData.h`。
- 插件接口：`include/PluginInterface.h`、插件管理器 `TrafficMonitor/PluginManager.h`/`PluginManager.cpp`。
- 绘制层：`TrafficMonitor/IDrawCommon.h`、`TrafficMonitor/DrawCommon.h`、`TrafficMonitor/TaskBarDlgDrawCommon.h`、`TrafficMonitor/DrawCommonFactory.h`。
