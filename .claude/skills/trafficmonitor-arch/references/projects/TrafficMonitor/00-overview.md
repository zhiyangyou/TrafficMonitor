# TrafficMonitor 项目总览

> 范围：仓库主项目 `TrafficMonitor/`。本文只给出"地图"——目录、文件分组、入口、关键全局对象一览、与兄弟项目的关系。实现细节、绘制 API、INI 字段全集、皮肤 XML 等不在此处展开，留给 `topics/` 下的专题文档。

## 4.1 项目基本信息

- 项目 GUID：`{09483BED-B1E9-4827-8120-A18302C84AA8}`（`TrafficMonitor.sln:6`、`TrafficMonitor/TrafficMonitor.vcxproj:54`）。
- 根命名空间 / RootNamespace：`TrafficMonitor`（`TrafficMonitor/TrafficMonitor.vcxproj:55`）。
- 输出类型：MFC Application（`Application`），子系统 `Windows`（`TrafficMonitor.vcxproj:288,328,366,405,444,482,523,566,597,639,683,724`）。
- 链接器依赖：`OpenHardwareMonitorApi.lib`、`pdh.lib`、`Powrprof.lib`、`Dwmapi.lib`（非 Lite 配置见 `TrafficMonitor.vcxproj:289,367,406,526,612,655`）；Lite 配置去掉 `OpenHardwareMonitorApi.lib`。
- 平台：Win32 / x64 / ARM64EC；Debug / Release / Debug (lite) / Release (lite) 共 12 组合（见 `TrafficMonitor.vcxproj:4-52`）。
- 链接顺序：必依赖 `OpenHardwareMonitorApi`（`TrafficMonitor.sln:7-9` 显式声明 ProjectDependencies）。

## 4.2 项目结构（按目录与子目录）

```
TrafficMonitor/                            # 主项目根目录（GUID 09483BED...）
├─ TrafficMonitor.cpp / .h                # 入口：CTrafficMonitorApp (CWinApp + ITrafficMonitor)
├─ TrafficMonitorDlg.cpp / .h             # 主对话框；持有 m_tBarDlg (CTaskBarDlg*)
├─ TrafficMonitor.rc / resource.h         # 主资源（菜单、字符串、图标）
├─ stdafx.cpp / stdafx.h / targetver.h    # 预编译头与 Windows 版本宏
├─ print_compile_time.bat                 # PreBuild 钩子：生成 compile_time.txt
├─ compile_time.txt                       # PreBuild 产物
│
├─ res/                                   # 资源文件
│   ├─ TrafficMonitor.ico                 # 应用图标
│   ├─ TrafficMonitor.rc2                 # 资源附加（VS 维护）
│   ├─ notifyicon*.ico                    # 托盘图标 (5 个)
│   ├─ notify_preview.bmp                 # 托盘预览
│   ├─ about_background.bmp               # "关于"对话框背景
│   ├─ menu_icon/                         # 菜单图标 (16x16)
│   ├─ Acknowledgement.txt / _en.txt      # 致谢
│
├─ language/                              # 多语言 INI 文件
│   ├─ English.ini
│   ├─ Simplified_Chinese.ini
│   └─ Traditional_Chinese.ini
│
├─ PdhHardwareQuery/                      # PDH 数据采集子模块
│   ├─ PdhQuery.h / .cpp                  # 基类 CPdhQuery：HQUERY + HCOUNTER
│   ├─ CPUUsage.h / .cpp                  # CCPUUsage
│   ├─ CpuFreq.h / .cpp                   # CPdhCpuFreq
│   ├─ GpuUsage.h / .cpp                  # CPdhGPUUsage
│   └─ DiskUsage.h / .cpp                 # CPdhDiskUsage
│
├─ tinyxml2/                              # vendored
│   └─ tinyxml2.h / .cpp
│
├─ skins/                                 # 默认皮肤目录（运行时创建/加载）
│
├─ *.cpp / *.h                            # 业务源文件（见 4.2.1 业务模块分组）
```

### 4.2.1 业务源文件分组（按主题）

下面把所有非入口/资源文件按主题分类，每个文件后给出 `声明→实现` 文件路径。

**应用对象与设置**
- `TrafficMonitor.cpp` / `TrafficMonitor.h` —— `CTrafficMonitorApp`（CWinApp + ITrafficMonitor）。
- `CommonData.cpp` / `CommonData.h` —— 全局结构体：`MainConfigData`、`MainWndSettingData`、`TaskBarSettingData`、`GeneralSettingData`、`SkinSettingData`、`PublicSettingData`、`DispStrings`、`FontInfo`、`LanguageInfo`、`StringSet`、枚举（`SpeedUnit`、`HardwareItem`、`DoubleClickAction`、`ColorMode`、`HistoryTrafficViewType`、`MemoryDisplay`）。
- `Common.cpp` / `Common.h` —— `CCommon` 静态函数库（路径、字符串、字节单位换算、写日志、获取 Windows 主题色、版本信息等）。
- `IniHelper.cpp` / `IniHelper.h` —— `CIniHelper`：纯头/实现对应的 INI 解析（UTF-8 / ANSI），提供 `GetString/WriteString/GetInt/WriteBool/...`。
- `SettingsHelper.cpp` / `SettingsHelper.h` —— `CSettingsHelper`：高级设置读写（任务栏颜色、显示文本、字体、插件显示文本）。
- `CVariant.cpp` / `CVariant.h` —— `CVariant`：`LoadTextFormat` 用的占位参数类型。
- `Nullable.hpp` —— C++ 模板 `CNullable`/`Nullable`（栈上可选对象），被多个模板使用。
- `StrTable.cpp` / `StrTable.h` —— `CStrTable`：从 `language\*.ini` 读取 stringtable，提供 `LoadText` / `LoadTextFormat` / `LoadMenuText`。
- `TaskbarDefaultStyle.cpp` / `TaskbarDefaultStyle.h` —— `CTaskbarDefaultStyle`：4 套任务栏颜色预设。
- `TaskbarItemOrderHelper.cpp` / `TaskbarItemOrderHelper.h` —— `CTaskbarItemOrderHelper`：项目顺序持久化与解析。
- `DisplayItem.cpp` / `DisplayItem.h` —— 内置 `DisplayItem` 枚举 + `CommonDisplayItem` 包装（统一内置项与插件项）。
- `language.h` —— 字符串 ID/键常量。
- `DllFunctions.cpp` / `DllFunctions.h` —— `CDllFunctions`：`GetDpiForMonitor` 等按需 API 的延迟加载。
- `WindowsSettingHelper.cpp` / `WindowsSettingHelper.h` —— Win10 深浅色判断、`.NET 4.5` 检测。
- `auto_start_helper.cpp` / `auto_start_helper.h` —— 注册表 + 任务计划两种自启动。
- `crashtool.cpp` / `crashtool.h` —— `CRASHREPORT::StartCrashReport()` 启动崩溃 dump。
- `UpdateHelper.cpp` / `UpdateHelper.h` —— `CUpdateHelper`：多源（GitHub/Gitee）拉取版本/更新内容。
- `HighResolutionTimer.h` —— 高精度计时器（用 `QueryPerformanceCounter`）。
- `FilePathHelper.cpp` / `FilePathHelper.h` —— 路径处理辅助。
- `Test.cpp` / `Test.h` —— 调试用 `CTest::Test()`（在 `_DEBUG` 下于 `InitInstance` 末尾被调用，`TrafficMonitor.cpp:1089-1091`）。

**主对话框与辅助对话框**
- `TrafficMonitorDlg.cpp` / `TrafficMonitorDlg.h` —— 主对话框 + 采集线程 + 历史流量读写 + 托盘。
- `BaseDialog.cpp` / `BaseDialog.h` —— `CBaseDialog` 基类（被大多数设置对话框继承）。
- `OptionsDlg.cpp` / `OptionsDlg.h` —— `COptionsDlg` 选项设置容器（多个 Tab）。
- `GeneralSettingsDlg.cpp` / `GeneralSettingsDlg.h` —— 常规设置 Tab。
- `MainWndSettingsDlg.cpp` / `MainWndSettingsDlg.h` —— 主窗口设置 Tab。
- `MainWndColorDlg.cpp` / `MainWndColorDlg.h` —— 主窗口颜色子设置。
- `TaskBarSettingsDlg.cpp` / `TaskBarSettingsDlg.h` —— 任务栏窗口设置 Tab。
- `TaskbarColorDlg.cpp` / `TaskbarColorDlg.h` —— 任务栏颜色子设置。
- `CAutoAdaptSettingsDlg.cpp` / `CAutoAdaptSettingsDlg.h` —— 自动适应主题色/皮肤设置。
- `DisplayTextSettingDlg.cpp` / `DisplayTextSettingDlg.h` —— 显示文本自定义。
- `SetItemOrderDlg.cpp` / `SetItemOrderDlg.h` —— 拖拽排序任务栏显示项。
- `SelectConnectionsDlg.cpp` / `SelectConnectionsDlg.h` —— 选择要显示的网卡。
- `IconSelectDlg.cpp` / `IconSelectDlg.h` —— 选择托盘图标。
- `CSkinPreviewView.cpp` / `CSkinPreviewView.h` —— 皮肤预览视图（被 `SkinDlg` 引用）。
- `SkinDlg.cpp` / `SkinDlg.h` —— 选择/管理皮肤。
- `SkinAutoAdaptSettingDlg.cpp` / `SkinAutoAdaptSettingDlg.h` —— 皮肤自动适应设置。
- `SkinManager.cpp` / `SkinManager.h` —— `CSkinManager`：单例，枚举所有皮肤并缓存每个皮肤的 `SkinSettingData`。
- `SkinFile.cpp` / `SkinFile.h` —— `CSkinFile`：解析单个皮肤 xml，暴露 `SkinInfo`、布局 (`Layout`)、对齐 (`Alignment`)。
- `MessageDlg.cpp` / `MessageDlg.h` —— 通用消息对话框。
- `NetworkInfoDlg.cpp` / `NetworkInfoDlg.h` —— "网络连接详情"对话框。
- `HistoryTrafficDlg.cpp` / `HistoryTrafficDlg.h` —— 历史流量主对话框。
- `HistoryTrafficListDlg.cpp` / `HistoryTrafficListDlg.h` —— 历史流量列表子对话框。
- `HistoryTrafficCalendarDlg.cpp` / `HistoryTrafficCalendarDlg.h` —— 历史流量日历子对话框。
- `HistoryTrafficListCtrl.cpp` / `HistoryTrafficListCtrl.h` —— 历史流量列表控件。
- `CalendarHelper.cpp` / `CalendarHelper.h` —— 日历辅助。
- `HistoryTrafficFile.cpp` / `HistoryTrafficFile.h` —— `CHistoryTrafficFile`：历史流量持久化（`history_traffic.dat`）。
- `AppAlreadyRuningDlg.cpp` / `AppAlreadyRuningDlg.h` —— "已有实例在运行"对话框。
- `AboutDlg.cpp` / `AboutDlg.h` —— "关于"对话框。
- `AdapterCommon.cpp` / `AdapterCommon.h` —— 适配器（网卡）辅助函数与 `NetWorkConection` 结构。
- `ColorSettingListCtrl.cpp` / `ColorSettingListCtrl.h` —— 颜色设置列表控件。
- `CTabCtrlEx.cpp` / `CTabCtrlEx.h` —— `CCTabCtrlEx`：扩展 TabCtrl。
- `TabDlg.cpp` / `TabDlg.h` —— `CTabDlg`：被 `COptionsDlg` 使用的 Tab 容器。
- `CMFCColorDialogEx.cpp` / `CMFCColorDialogEx.h` —— 颜色选择对话框扩展。

**任务栏窗口**
- `TaskBarDlg.cpp` / `TaskBarDlg.h` —— `CTaskBarDlg` 基类，定义三种派生共享的行为。
- `ClassicalTaskbarDlg.cpp` / `ClassicalTaskbarDlg.h` —— 经典任务栏（XP/Vista/Win7 风格）。
- `Win11TaskbarDlg.cpp` / `Win11TaskbarDlg.h` —— Windows 11 任务栏专用派生。
- `Win11TaskbarSettingDlg.cpp` / `Win11TaskbarSettingDlg.h` —— Win11 任务栏设置对话框。
- `WineTaskbarDlg.cpp` / `WineTaskbarDlg.h` —— Wine 环境专用派生。
- `TaskbarHelper.cpp` / `TaskbarHelper.h` —— `CTaskbarHelper`：与 `Shell_TrayWnd` 交互的辅助方法。
- `TaskBarDlgDrawCommon.cpp` / `TaskBarDlgDrawCommon.h` —— D2D/D3D10/DComposition 渲染管线的所有支撑类（`CTaskBarDlgDrawCommonSupport` / `CTaskBarDlgDrawCommonWindowSupport` / `CD2D1DeviceContextWindowSupport` / `CTaskBarDlgDrawCommon` / `CTaskBarDlgDrawBuffer` / 文本钩子）。
- `RenderAPISupport.h` —— 渲染 API 选择辅助声明。

**绘制层**
- `DrawCommon.cpp` / `DrawCommon.h` —— `CDrawCommon`（GDI 路径）、`CDrawDoubleBuffer`、`DrawCommonHelper` 命名空间。
- `DrawCommonEx.cpp` / `DrawCommonEx.h` —— `CDrawCommonEx`：`CDrawCommon` 扩展（含 alpha 通道、位图操作等）。
- `DrawCommonFactory.cpp` / `DrawCommonFactory.h` —— 渲染管线工厂（栈内存 union + 类型分发）。
- `DrawTextManager.cpp` / `DrawTextManager.h` —— `User32DrawTextManager`：被钩子替换后的 `DrawText{A,W,ExA,ExW}` 实现。
- `D2D1Support.cpp` / `D2D1Support.h` —— `CD2D1Device`：D2D1 device 封装。
- `D3D10Support1.cpp` / `D3D10Support1.h` —— `CD3D10Device1`：D3D10.1 device 封装。
- `DCompositionSupport.cpp` / `DCompositionSupport.h` —— `CDCompositionDevice`：DirectComposition device 封装。
- `Dxgi1Support2.cpp` / `Dxgi1Support2.h` —— DXGI 1.2 封装。
- `Image2DEffect.cpp` / `Image2DEffect.h` —— `CImage2DEffect`：D2D 2D 效果封装。
- `HResultException.cpp` / `HResultException.h` —— `CHResultException`：HRESULT 异常包装。
- `IDrawCommon.h` —— 抽象接口 `IDrawCommon` / `IDrawBuffer`。
- `WIC.cpp` / `WIC.h` —— `CWIC` 类：WIC 图像解码/编码辅助。
- `SupportedRenderEnums.cpp` / `SupportedRenderEnums.h` —— `CSupportedRenderEnums`：枚举当前支持的渲染类型。
- `WinVersionHelper.cpp` / `WinVersionHelper.h` —— `CWinVersionHelper`。
- `WindowsWebExperienceDetector.cpp` / `WindowsWebExperienceDetector.h` —— `WindowsWebExperienceDetector`：WinRT 检测 Web Experience Pack。
- `PictureStatic.cpp` / `PictureStatic.h` —— 图片静态控件。
- `StaticEx.cpp` / `StaticEx.h` —— 文本静态控件扩展。
- `ColorStatic.cpp` / `ColorStatic.h` —— 颜色块静态控件。
- `LinkStatic.cpp` / `LinkStatic.h` —— 超链接静态控件。
- `ListCtrlEx.cpp` / `ListCtrlEx.h` —— 扩展 ListCtrl。
- `ComboBox2.cpp` / `ComboBox2.h` —— 扩展 ComboBox。
- `SpinEdit.cpp` / `SpinEdit.h` —— 微调编辑框。
- `FileDialogEx.cpp` / `FileDialogEx.h` —— 文件对话框扩展。

**插件层**
- `PluginManager.cpp` / `PluginManager.h` —— `CPluginManager`：DLL 加载、IAT 钩子、版本/项目枚举。
- `PluginManagerDlg.cpp` / `PluginManagerDlg.h` —— 插件管理对话框（启用/禁用、查看信息）。
- `PluginInfoDlg.cpp` / `PluginInfoDlg.h` —— 插件详情对话框。
- `PluginUpdateHelper.cpp` / `PluginUpdateHelper.h` —— `CPluginUpdateHelper`：插件市场版本信息。
- `TinyXml2Helper.cpp` / `TinyXml2Helper.h` —— tinyxml2 包装（与 `tinyxml2/` 共用）。

**第三方 vendored**
- `tinyxml2/tinyxml2.h` / `tinyxml2/tinyxml2.cpp` —— 第三方 XML 解析器，编译时 `NotUsing` 预编译头。

**外部包含**
- `../include/PluginInterface.h` —— 插件接口定义（`ITMPlugin` / `IPluginItem` / `ITrafficMonitor`），通过 `<ClInclude Include="..\include\PluginInterface.h" />` 加入主项目（`TrafficMonitor.vcxproj:773`）。

## 4.3 入口：CTrafficMonitorApp / InitInstance

### 4.3.1 进程入口
MFC 应用对象（`extern CTrafficMonitorApp theApp`）作为模块入口；CRT 启动后调用 `CTrafficMonitorApp::InitInstance`。

### 4.3.2 构造（`TrafficMonitor.cpp:41-57`）
```
CTrafficMonitorApp::CTrafficMonitorApp() {
    self = this;                                       // 早于 theApp 初始化可用的 self
    CRASHREPORT::StartCrashReport();
    if (m_win_version.IsWindows11OrLater())             // 守 DISABLE_WINDOWS_WEB_EXPERIENCE_DETECTOR
        winrt::init_apartment();
    CheckWindows11Taskbar();                           // 见 TrafficMonitor.cpp:1298-1310
    m_theme_color = CCommon::GetWindowsThemeColor();
}
```

### 4.3.3 InitInstance 步骤摘要（`TrafficMonitor.cpp:915-1131`）

| 阶段 | 行号 | 作用 |
| --- | --- | --- |
| 1. 改类名 | 918-921 | 把 MFC 默认对话框类名 `#32770` 改为 `APP_CLASS_NAME`（避免被 FindWindow 错认） |
| 2. 路径计算 | 923-957 | 算出 `m_module_path / m_module_dir / m_appdata_dir / m_config_path / m_history_traffic_path / m_log_path / m_skin_path` |
| 3. 加载 global_cfg | 942 | `LoadGlobalConfig()` 决定 `portable_mode` |
| 4. 多实例检测 | 979-1013 | 用 `CreateMutex` 检查是否已有实例 |
| 5. 加载插件 | 1015-1017 | `m_plugins.LoadPlugins()` |
| 6. 加载 INI 配置 | 1019-1022 | `LoadConfig()` + `m_taskbar_default_style.LoadConfig()` |
| 7. 启动更新线程 | 1057-1061 | `CheckUpdateInThread(false)` |
| 8. 启动 OHM 后台线程 | 1063-1086 | 需 `.NET 4.5` + 至少一项硬件监控开启 + 无 `WITHOUT_TEMPERATURE` |
| 9. 启动 GDI+ | 1093-1095 | `GdiplusStartup(&m_gdiplusToken, ...)` |
| 10. 下发设置 | 1097 | `SendSettingsToPlugin()` |
| 11. DoModal | 1099-1101 | `CTrafficMonitorDlg dlg; dlg.DoModal()` —— 阻塞运行直到对话框关闭 |

### 4.3.4 ExitInstance（`TrafficMonitor.cpp:1382-1388`）
仅释放 GDI+ token；`m_pMonitor` 由 `shared_ptr` 析构，`m_plugins` 析构时调 `FreeLibrary` 卸载每个 dll（`PluginManager.cpp:21-25`）。

### 4.3.5 资源清理顺序
- `theApp` 析构：先析构各值成员（`m_plugins` 析构触发 `FreeLibrary`），再基类 `CWinApp` 析构。
- GDI+ token：必须先于全局对象析构关闭，所以放在 `ExitInstance` 显式调用。
- OpenHardwareMonitor 后台线程：`m_pMonitor` 析构时由 `shared_ptr` 释放；如果 `OpenHardwareMonitorApi` 内部有后台线程需等待，需在 `m_pMonitor.reset()` 之前 join（`TrafficMonitor.cpp` 当前未显式 join——这是一个值得关注的悬挂点，但不在本文档范围内）。

## 4.4 关键全局对象一览

> 这里只列"`类名` + 头文件 + 一句话职责"。更详细的字段、API 留在 `topics/`。

| 类 / 对象 | 文件 | 职责一句话 |
| --- | --- | --- |
| `theApp` (`CTrafficMonitorApp`) | `TrafficMonitor.h` / `TrafficMonitor.cpp` | 进程级全局：同时是 CWinApp 和 ITrafficMonitor；持有路径/数据/设置/插件/渲染资源 |
| `CTrafficMonitorApp::self` | `TrafficMonitor.h:35`，`TrafficMonitor.cpp:37` | 构造期 `self = this`，用于早于 theApp 初始化的代码 |
| `CTaskbarDefaultStyle m_taskbar_default_style` | `TrafficMonitor.h:88`，`TaskbarDefaultStyle.h` | 4 套任务栏颜色预设方案（深色/浅色自动切换） |
| `CPluginManager m_plugins` | `TrafficMonitor.h:89`，`PluginManager.h` | 唯一的插件加载器 + 钩子改写器 + 项目枚举器 |
| `CStrTable m_str_table` | `TrafficMonitor.h:91`，`StrTable.h` | 加载 `language\*.ini` 的 stringtable，提供 `LoadText`/`LoadTextFormat` |
| `CPluginUpdateHelper m_plugin_update` | `TrafficMonitor.h:92`，`PluginUpdateHelper.h` | 启动时拉取插件市场版本（`CheckUpdateThreadFunc` 调用） |
| `CDllFunctions m_dll_functions` | `TrafficMonitor.h:90`，`DllFunctions.h` | 按需 LoadLibrary 包装（`GetDpiForMonitor` 等） |
| `CWinVersionHelper m_win_version` | `TrafficMonitor.h:84`，`WinVersionHelper.h` | 判断 Windows 版本（XP..11 + Wine） |
| `CLazyConstructable<CTaskBarDlgDrawCommonSupport> m_d2d_taskbar_draw_common_support` | `TrafficMonitor.h:108` | 任务栏 D2D 渲染全局设备懒构造（首次访问 `Get()` 时构造） |
| `shared_ptr<IOpenHardwareMonitor> m_pMonitor` | `TrafficMonitor.h:103-104`（`WITHOUT_TEMPERATURE` 守卫） | 硬件监控后端代理（封装 LibreHardwareMonitor） |
| `CCriticalSection m_minitor_lib_critical` | `TrafficMonitor.h:106` | 保护 `m_pMonitor` 跨线程访问的临界区 |
| `HICON m_notify_icons[MAX_NOTIFY_ICON]` | `TrafficMonitor.h:86` | 5 个托盘图标资源句柄缓存 |
| `std::map<UINT, HICON> m_menu_icons` | `TrafficMonitor.h:183` | 菜单图标资源按 ID 缓存 |
| `ULONG_PTR m_gdiplusToken` | `TrafficMonitor.h:185` | GDI+ 启动 token（`InitInstance` 分配、`ExitInstance` 释放） |
| `CMenu m_main_menu / m_taskbar_menu / m_main_menu_plugin / m_taskbar_menu_plugin / m_main_menu_plugin_sub_menu / m_taskbar_menu_plugin_sub_menu` | `TrafficMonitor.h:94-99` | 6 个主/任务栏的右键菜单模板（在 `InitMenuResourse` 注入插件子菜单） |
| `MainConfigData m_cfg_data` | `TrafficMonitor.h:75` | 主程序"选项"类设置：透明度、皮肤、位置、连接、视图类型等 |
| `MainWndSettingData m_main_wnd_data` | `TrafficMonitor.h:71` | 主窗口渲染参数（字体、显示文本、颜色、单位、双击动作等） |
| `TaskBarSettingData m_taskbar_data` | `TrafficMonitor.h:72` | 任务栏窗口渲染参数（背景、状态条、显示项集合、布局、对齐、网速图等） |
| `GeneralSettingData m_general_data` | `TrafficMonitor.h:73` | 常规设置（语言、更新源、自启动、监控时间间隔、硬件监控使能、便携模式等） |
| `unsigned __int64 m_in_speed/m_out_speed/m_today_up_traffic/m_today_down_traffic` | `TrafficMonitor.h:48,62-63` | 网速 / 今日流量（被所有窗口共享） |
| `int m_cpu_usage/m_memory_usage/m_used_memory/m_total_memory` | `TrafficMonitor.h:50-53` | CPU / 内存 |
| `float m_cpu_temperature/m_cpu_freq/m_gpu_temperature/m_hdd_temperature/m_main_board_temperature` | `TrafficMonitor.h:54-58` | 温度 / 频率 |
| `int m_gpu_usage/m_hdd_usage` | `TrafficMonitor.h:59-60` | GPU / 硬盘占用 |
| `int m_dpi` | `TrafficMonitor.h:179` | 主窗口 DPI 缓存 |
| `bool m_is_windows11_taskbar` | `TrafficMonitor.h:187` | Win11 任务栏判定（`CheckWindows11Taskbar`） |
| `COLORREF m_theme_color` | `TrafficMonitor.h:188` | Windows 主题色 |
| `CTrafficMonitorDlg* m_pMainWnd` | CWinApp 字段 | MFC 标准字段；`InitInstance` 中被赋 `&dlg`（`TrafficMonitor.cpp:1100`） |
| `CTaskBarDlg* m_tBarDlg` | `TrafficMonitorDlg.h:58` | 主对话框内嵌的任务栏窗口对象（懒创建） |
| `CSkinFile m_skin` | `TrafficMonitorDlg.h:104` | 当前正在使用皮肤的 `CSkinFile` 实例 |
| `CHistoryTrafficFile m_history_traffic` | `TrafficMonitorDlg.h:120` | 历史流量文件对象，构造时绑 `theApp.m_history_traffic_path` |
| `unsigned int CTrafficMonitorDlg::m_WM_TASKBARCREATED` | `TrafficMonitorDlg.h:115`，`TrafficMonitorDlg.cpp:31` | 通过 `RegisterWindowMessage(L"TaskbarCreated")` 注册，监听任务栏重建 |

## 4.5 与其他项目的关系

```
                            include/PluginInterface.h
                                   ▲
                ┌──────────────────┴──────────────────┐
                │                                     │
        TrafficMonitor/                          PluginDemo/
        (主 EXE)                                (示例 DLL, 独立)
                │
                │ 静态链接
                ▼
        OpenHardwareMonitorApi/
        (C++/CLI DLL)
                │
                │ 依赖
                ▼
        LibreHardwareMonitorLib.dll
        (随项目分发, x64 / x86 / ARM64EC 各一份)
```

### 4.5.1 与 `OpenHardwareMonitorApi` 的关系
- **依赖方向**：`TrafficMonitor` 静态链接 `OpenHardwareMonitorApi.lib`（非 Lite 配置，见 `TrafficMonitor.vcxproj:289,367,406,526,612,655`）；运行期通过 `OpenHardwareMonitorApi::CreateInstance()` 拿到 `IOpenHardwareMonitor*`（`TrafficMonitor.cpp:625`）。
- **接口来源**：`include/OpenHardwareMonitor/OpenHardwareMonitorApi.h` 定义纯虚接口 `IOpenHardwareMonitor`（含 `GetHardwareInfo/CpuTemperature/GpuTemperature/...`），被 `CTrafficMonitorApp::m_pMonitor` 使用。
- **使用方式**：`theApp.m_pMonitor->CpuTemperature()` 等被 `DoMonitorAcquisition` 调用于采集线程，访问前先加 `m_minitor_lib_critical`（`TrafficMonitor.cpp:1146`）。
- **Lite 版差异**：Lite 配置编译时 `WITHOUT_TEMPERATURE` 被定义，`m_pMonitor` 字段不存在（`TrafficMonitor.h:101-104` 的 `#ifndef` 守卫），`DoMonitorAcquisition` 中相关代码全部条件编译排除。`TrafficMonitor_Lite.sln` 直接不引用 `OpenHardwareMonitorApi` 项目。

### 4.5.2 与 `PluginDemo` 的关系
- **`TrafficMonitor` 不依赖 `PluginDemo`**——`PluginDemo` 是给插件作者参考的样例 DLL。
- 二者唯一的"关系"是同时引用 `../include/PluginInterface.h`（`TrafficMonitor.vcxproj:773`）。
- 编译产物会落在各自 `Bin/<Config>/` 目录；运行时 `TrafficMonitor` 加载 `plugins\*.dll`，**只要导出 `TMPluginGetInstance` 即可**，与 `PluginDemo` 没有强耦合。
- PluginDemo 自身结构：
  - `PluginDemo.h` / `PluginDemo.cpp` —— `CPluginDemo`（`ITMPlugin` 实现，单例 + 3 个 IPluginItem：日期、时间、自定义绘制）。
  - `PluginSystemDate.h/.cpp`、`PluginSystemTime.h/.cpp`、`CustomDrawItem.h/.cpp` —— 三个具体 item。
  - `DataManager.h/.cpp`、`OptionsDlg.h/.cpp` —— Demo 内部数据与选项。

### 4.5.3 与仓库根目录非项目资源的关系
- `version.info` / `version_utf8.info` —— 外部维护的版本号，主程序在 INI 写入时回写 `L"app" / L"version" / VERSION`（`TrafficMonitor.cpp:442`）。
- `Help.md` / `Help_en-us.md` —— 帮助文档，被"常见问题"菜单项通过 GitHub 链接打开（`TrafficMonitor.cpp:1341-1357`）。
- `UpdateLog/`、`README.md`、`LICENSE*`、`Screenshots/`、`images/`、`皮肤制作教程.md` —— 文档与发布物，不参与编译。

## 4.6 与本节相关的关键文件索引

- 入口：`TrafficMonitor/TrafficMonitor.cpp`、`TrafficMonitor/TrafficMonitor.h`。
- 主对话框：`TrafficMonitor/TrafficMonitorDlg.cpp` / `.h`。
- 设置结构全集：`TrafficMonitor/CommonData.h`。
- 渲染管线：`TrafficMonitor/TaskBarDlgDrawCommon.h`（D2D/D3D/DComposition）、`TrafficMonitor/DrawCommon.h`（GDI）、`TrafficMonitor/DrawCommonFactory.h`。
- 插件层：`TrafficMonitor/PluginManager.cpp`、`include/PluginInterface.h`。
- 兄弟项目：`OpenHardwareMonitorApi/OpenHardwareMonitorApi.vcxproj`、`PluginDemo/PluginDemo.vcxproj`、`TrafficMonitor.sln`、`TrafficMonitor_Lite.sln`。
- 项目文件本身：`TrafficMonitor/TrafficMonitor.vcxproj`、`TrafficMonitor/TrafficMonitor.vcxproj.filters`。
