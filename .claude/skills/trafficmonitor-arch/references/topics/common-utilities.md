# 通用工具与基础设施层

本文件覆盖 `TrafficMonitor/` 下与具体业务（流量/硬件/皮肤/插件）解耦的工具类、基础设施、UI 控件族与异常/动态库辅助。属于"目录地图+职责一句话"性质。

## 字符串 / 路径 / 单位工具

- **`Common.h` / `Common.cpp`** — `CCommon` 静态工具类。位于 `TrafficMonitor/Common.h:7-262`，提供：
  - 编码转换：`StrToUnicode` / `UnicodeToStr` / `AsciiToUnicode` / `AsciiToStr`（`Common.h:13-18`）。
  - 字符串归一化、分割、大小写转换、替换、相似度（编辑距离）：`Common.h:20-34, 175-183`。
  - 文件读写与目录遍历：`GetFileContent` / `GetFiles` / `FileExist` / `IsFolder` / `MoveAFile`（`Common.h:37-44, 91-104`）。
  - 单位 / 数据格式化：`DataSizeToString` / `TemperatureToString` / `UsageToString` / `FreqToString` / `KBytesToString`（`Common.h:50-66`）。
  - 路径：`GetModuleDir` / `GetSystemDir` / `GetTemplateDir` / `GetAppDataConfigDir`（`Common.h:113-122`）。
  - 时间：`CompareFileTime2` / `CompareSystemTime` / `GetCurrentTimeSinceEpochMilliseconds`（`Common.h:69, 107, 110`）。
  - 日志、快捷方式、剪贴板、URL 抓取、外网 IP：`WriteLog` / `CreateFileShortcut` / `CopyStringToClipboard` / `GetURL` / `GetInternetIp` / `GetInternetIp2`（`Common.h:72-85, 138-149`）。
  - 资源 / 文本：`LoadText` / `LoadTextFormat` / `StringFormat`（占位符 `<%序号%>`）/ `GetTextResource` / `GetLastCompileTime` / `LoadIconResource` / `LoadMenuResource`（`Common.h:154-211`）。
  - 颜色 / 字体 / 数字位：`DrawWindowText` / `NormalizeFont` / `TransparentColorConvert` / `IsColorSimilar` / `CountOneBits` / `SetNumberBit` / `GetNumberBit`（`Common.h:125, 170, 193-213, 218-222`）。
  - 杂项：`IsForegroundFullscreen` / `SetThreadLanguage` / `SetColorMode` / `SetDialogFont` / `GetWindowsThemeColor` / `GetErrorMessage` / `GetMenuItemPosition`（`Common.h:135, 186-211, 259-261`）。
  - 模板：`RemoveVectorDuplicateItem` / `ValidatValue`（值域裁剪）（`Common.h:225-256`）。
  - 头文件末尾还提供 `GetArrayLength`（`Common.h:271-275`）和 `CStaticVariableWrapper` / `MakeStaticVariableWrapper`（`Common.h:282-342`）以及 `Destroy` / `EmplaceAt`（`Common.h:344-360`）等低层模板。
- **`CommonData.h` / `CommonData.cpp`** — 跨窗口共享的常量与配置结构。位于 `TrafficMonitor/CommonData.h`：
  - 基础数据：`Date` / `HistoryTraffic`（`CommonData.h:6-32`）。
  - 颜色宏：`TRAFFIC_COLOR_*`（`CommonData.h:35-39`）。
  - 枚举：`SpeedUnit`（`CommonData.h:42-47`）、`HardwareItem`（位掩码，`CommonData.h:51-57`）、`DoubleClickAction`（`CommonData.h:82-92`）、`ColorMode`（`CommonData.h:95-99`）、`HistoryTrafficViewType`（`CommonData.h:148-155`）、`MemoryDisplay`（`CommonData.h:224-229`）。
  - 显示文本 / 字体 / 字符串集合：`DispStrings`（`CommonData.h:60-79`）、`FontInfo` + `FontSizeToLfHeight`（`CommonData.h:101-145`）、`StringSet`（`CommonData.h:157-169`）、`LanguageInfo`（`CommonData.h:171-187`）。
  - 主配置结构：`MainConfigData`（`CommonData.h:190-221`）、`PublicSettingData`（`CommonData.h:244-260`，被 `MainWndSettingData` / `TaskBarSettingData` 继承）、`MainWndSettingData`（`CommonData.h:264-276`）、`SkinSettingData`（`CommonData.h:232-241`）、`TaskBarSettingData`（`CommonData.h:292-350`，包含任务栏窗口所有可调项）、`GeneralSettingData`（`CommonData.h:353-401`，包含 `IsHardwareEnable` / `SetHardwareEnable` 位操作与 `MONITOR_TIME_SPAN_MIN/MAX` 上下界宏 `CommonData.h:404-405`）。
  - 简易 RAII：`CFlagLocker`（`CommonData.h:408-424`），构造置 true 析构置 false。

## 配置 / INI 持久化

- **`IniHelper.h` / `.cpp`** — `CIniHelper`，UTF-8 BOM 友好的 ini 读写器（默认 UTF-8 写，可切 ANSI）。支持从文件（`IniHelper.h:13`）或资源（`IniHelper.h:15`）加载；提供 `WriteString` / `GetString` / `WriteInt` / `GetInt` / `WriteBool` / `GetBool` / 数组 / 字符串列表 / `GetAllAppName` / `GetAllKeyValues` / `RemoveSection` / `Save` 等（`IniHelper.h:23-45`）。私有 `UnEscapeString` / `_WriteString` / `_GetString` / `MergeStringList` / `SplitStringList`（`IniHelper.h:52-57`）。
- **`SettingsHelper.h` / `.cpp`** — `CSettingsHelper : public CIniHelper`，针对主题颜色、字体、显示文本的复合读写。`SaveFontData` / `LoadFontData`（`SettingsHelper.h:11-12`）、`LoadMainWndColors` / `SaveMainWndColors`（`SettingsHelper.h:21, 29`）、`LoadTaskbarWndColors` / `SaveTaskbarWndColors`（`SettingsHelper.h:31-33`，`TaskbarItemColor` 来自 `CommonData.h:280-289`）、`LoadDisplayStr` / `SaveDisplayStr` / `LoadPluginDisplayStr` / `SavePluginDisplayStr`（`SettingsHelper.h:35-39`）。

## Variant / Nullable 容器

- **`Nullable.hpp`** — 模板基础设施：
  - `AlignedStorage<T>`（`Nullable.hpp:8-29`）：对齐字节缓冲。
  - `NullableDefaultDeleter<T>`（`Nullable.hpp:37-44`）。
  - `CNullable<T, Deleter>`（`Nullable.hpp:51-202`）：可空对象，支持拷贝/移动、`Construct` / `Get` / `GetUnsafe` / `HasValue` / `operator bool`，未构造时 `Get` 抛 `CallNullObjectError`。
  - `MakeNullableObject`（`Nullable.hpp:203-208`）。
  - `CLazyConstructable<T, Deleter>`（`Nullable.hpp:216-237`）：延迟构造包装，`Get()` 时如未构造则默认构造。
  - `CLazyConstructableWithInitializer<T, Deleter, Container<InitArgs...>>`（`Nullable.hpp:239-288`）：带初始化参数容器的延迟构造版本。
  - `DefaultCLazyConstructableWithInitializer` 别名（`Nullable.hpp:290-292`）。
- **`CVariant.h` / `.cpp`** — `CVariant`（`CVariant.h:2-24`），接受 int / size_t / double / LPCTSTR / CString / wstring 构造，统一 `ToString()` 转为 `CString`，用于 `CCommon::StringFormat` / `LoadTextFormat` 的占位参数列表。

## 文件路径与对话框

- **`FilePathHelper.h` / `.cpp`** — `CFilePathHelper`（`FilePathHelper.h:2-22`）：从一个完整路径中提取扩展名（可选大写、带点）、文件名、不含扩展名的文件名、所在目录、上级目录、原始路径；`ReplaceFileExtension` 原地替换扩展名。
- **`FileDialogEx.h` / `.cpp`** — `CFileDialogEx`（`FileDialogEx.h:3-25`），包装 `IFileDialog`（Vista+ 通用对话框），支持 `DoModal` / `GetPathName` / `GetFileName`，内部把经典 `lpszFilter` 字符串解析为 `COMDLG_FILTERSPEC` 列表。

## Windows 版本 / 系统能力探测

- **`WinVersionHelper.h` / `.cpp`** — `CWinVersionHelper`（`WinVersionHelper.h:2-25`）：缓存主版本/次版本/Build 号，提供 `IsWindows11OrLater` / `IsWindows10FallCreatorOrLater` / `IsWindows7` / `IsWindows8Or8point1` / `IsWindows8Point1OrLater` / `IsWindows8OrLater` / `IsWindows10OrLater` / `IsWine` 判断。
- **`WindowsSettingHelper.h` / `.cpp`** — `CWindowsSettingHelper`（`WindowsSettingHelper.h:3-20`）：静态类，提供 `IsWindows10LightTheme` / `CheckWindows10LightTheme` / `IsDotNetFramework4Point5Installed` / `IsTaskbarShowingInAllDisplays` / `IsTaskbarWidgetsBtnShown`（Win11 任务栏小工具按钮） / `IsTaskbarCenterAlign`（Win11 居中任务栏），内部走注册表 `GetDWORDRegKeyData`（`WindowsSettingHelper.h:15-16`）。
- **`WindowsWebExperienceDetector.h` / `.cpp`** — `WindowsWebExperienceDetector::IsDetected()`（`WindowsWebExperienceDetector.h:3-7`）：检测 Windows Web Experience 小组件包是否安装；与 `TaskBarSettingData::is_windows_web_experience_detected` 字段（`CommonData.h:349`）配合用于副显示器上避让小组件。

## 计时器

- **`HighResolutionTimer.h`** — `CHighResolutionTimer`（`HighResolutionTimer.h:4-59`），封装 `timeSetEvent`/`timeKillEvent`（WinMM），提供 `CreateTimer(dwUser, uDelay, lpTimeProc)` 与 `KillTimer()`；静态 `TimeProc` 回调把 `DWORD_PTR` 还原回 `CHighResolutionTimer*` 再调用户回调。

## 绘制后端的具体实现

> 抽象与"选优策略"归 `render-api.md` 主题文档；本节只列各后端实现文件与其职责。

- **`WIC.h` / `.cpp`** — `CWICFactory`（`WIC.h:7-21`）：进程级唯一的 `IWICImagingFactory` 持有者，析构时释放 OLE 初始化引用（`m_instance` 静态，`WIC.h:17`）。`CMenuIcon`（`WIC.h:23-39`）使用 WIC 把 `HICON` 转 32 位 PARGB 位图再贴到菜单项。`CWICException` 继承 `CHResultException`（`WIC.h:41-44`）。
- **`D2D1Support.h` / `.cpp`** — `CD2D1Exception` / `CDWriteException`（`D2D1Support.h:7-15`）；`CD2D1Support`（`D2D1Support.h:17-28`）提供 `CheckSupport` 与 `ID2D1Factory*` 工厂；`CD2D1Device`（`D2D1Support.h:30-46`）包装 `ID2D1Device` 的生命周期与 `Recreate`；`CD2D1Support1`（`D2D1Support.h:48-53`）对应 D2D1.1；`CDWriteSupport`（`D2D1Support.h:55-60`）对应 DirectWrite 工厂。
- **`D3D10Support1.h` / `.cpp`** — `CD3D10Exception1`（`D3D10Support1.h:13-16`）；`CD3D10Device1`（`D3D10Support1.h:18-76`）封装 `ID3D10Device1` 创建设备（`Data` 子结构含 adapter/driver type/feature level/sdk version 等），并提供 `Recreate` / `Get` / `GetAdapter` 与各种 setter；`CD3D10Support1::CheckSupport` / `GetDeviceList`（`D3D10Support1.h:78-84`）；`CDXShaderException`（`D3D10Support1.h:86-96`）；`ShaderMacro`（`D3D10Support1.h:98-102`）；`CShader`（`D3D10Support1.h:104-166`）编译 HLSL，管理宏/入口点/target/Include 路径并缓存字节码；`CD3D10DrawCallWaiter`（`D3D10Support1.h:168-183`）通过 Query 对象等待 GPU 绘制完成。
- **`Dxgi1Support2.h` / `.cpp`** — `CDxgiException`（`Dxgi1Support2.h:8-11`）；`CDxgiSwapChainResource<SwapChain>` 模板（`Dxgi1Support2.h:13-33`）定义 `OnSwapChainResizeBegin` / `OnSwapChainResizeEnd` 钩子；`CDxgiSwapChain1`（`Dxgi1Support2.h:39-55`）包装 `IDXGISwapChain1`，`Recreate` / `Resize` / `GetStorage`；`CDxgi1Support2::CheckSupport` / `GetFactory`（`Dxgi1Support2.h:57-62`）。注：源文件包含 `#pragma comment(lib, "DXGI.lib")`。
- **`DCompositionSupport.h` / `.cpp`** — `CDCompositionException`（`DCompositionSupport.h:7-10`）；`CDCompositionDevice`（`DCompositionSupport.h:12-29`）包装 `IDCompositionDevice`，`Recreate(IDXGIDevice*)` / `Get` / `GetStorage`；`CDCompositionSupport::CheckSupport`（`DCompositionSupport.h:31-35`）。
- **`Image2DEffect.h` / `.cpp`** — `D3DQuadrangle` 命名空间（`Image2DEffect.h:6-58`）：`VERTEX_INDEX_LIST`、VS 字节码 `GetVsShader()`、顶点结构模板 `QuadrangleVertexs<Vertex>`。`Image2DVertex`（`Image2DEffect.h:60-64`）。`CImage2DEffect`（`Image2DEffect.h:79-154`）以 `ID3D10Device1` 为输入，构造时编译 VS，封装输入/输出纹理、Input Layout / Vertex / Index Buffer / 光栅化 / 混合 / 深度模板状态、PS 资源视图与采样器；提供 `RebindDevice1` / `SetOutputSize` / `SetInputTexture` / `SetOutputTexture` / `SetVsByteCode` / `SetPsByteCode` / `ApplyPipelineConfig` / `Draw` / `DrawOnly` / `Clear` / `ClearOnly`；末尾提供 `CIMAGE2DEFFECT_SHADER_VS_*_DECLARATION` 宏（`Image2DEffect.h:155-167`）。

## MFC UI 控件族

- **`LinkStatic.h` / `.cpp`** — `CLinkStatic : public CStatic`（`LinkStatic.h:14-45`），超链接样式 Static。`SetURL` / `SetLinkIsURL` 切换点击行为（打开 URL 或向父窗发 `WM_LINK_CLICKED`，宏定义在 `LinkStatic.h:10`）。支持 hover 字体切换与背景色。
- **`StaticEx.h` / `.cpp`** — `CStaticEx : public CStatic`（`StaticEx.h:8-44`），彩色文本 Static。`SetWindowTextEx` / `SetTextColor` / `SetBackColor` / `GetString`（`StaticEx.h:25-28`），对齐枚举 `Alignment`（`StaticEx.h:17-22`）。
- **`ColorStatic.h` / `.cpp`** — `CColorStatic : public CStatic`（`ColorStatic.h:6-36`），单色或多色（数组 `m_colors`）色块 Static，点击发 `WM_STATIC_CLICKED`（`ColorStatic.h:4`）。支持 hover 与超链接光标。
- **`PictureStatic.h` / `.cpp`** — `CPictureStatic : public CStatic`（`PictureStatic.h:9-27`），从 `UINT` 资源 ID 或 `HBITMAP` 加载位图并双缓冲绘制，重绘时向父窗发 `WM_CONTROL_REPAINT`（`PictureStatic.h:7`，`wParam` 传自身指针、`lParam` 传 CDC 指针）。
- **`ComboBox2.h` / `.cpp`** — `CComboBox2 : public CComboBox`（`ComboBox2.h:6-22`），在 `PreTranslateMessage` 拦截滚轮，`SetMouseWheelEnable` 切换是否响应。
- **`ListCtrlEx.h` / `.cpp`** — `CListCtrlEx : public CListCtrl`（`ListCtrlEx.h:7-46`），单元格原地编辑（`IDC_ITEM_EDITBOX` 行 6，嵌入 `CEdit`），通过 `eEditColMethod` 枚举（`ListCtrlEx.h:17-22`）和 `SetEditableCol(initializer_list<int>)` 控制哪些列可编辑。
- **`SpinEdit.h` / `.cpp`** — `CSpinEdit : public CEdit`（`SpinEdit.h:8-38`），右侧附加 `CSpinButtonCtrl` 的数值输入。`SetRange(lower, upper, step)` / `SetValue` / `GetValue` / `SetMouseWheelEnable`；值变化发 `WM_SPIN_EDIT_POS_CHANGED`（`SpinEdit.h:6`）。
- **`TabDlg.h` / `.cpp`** — `CTabDlg : public CBaseDialog`（`TabDlg.h:5-45`），选项卡内的子对话框基类。提供 `OnTabEntered` / `OnSettingsApplied` / `SetControlMouseWheelEnable` 钩子（`TabDlg.h:15-19`），并实现垂直滚动（`SetScrollbarInfo` / `ScrollWindowSimple`），重写 `ResizeDynamicLayout` 修正动态布局调用时机（`TabDlg.h:38`）。
- **`BaseDialog.h` / `.cpp`** — `CBaseDialog : public CDialog`（`BaseDialog.h:8-99`），所有选项设置对话框的基类。提供：
  - 记忆大小 / 最小大小：`SetMinSize` / `m_remember_dlg_size` / `m_window_size`（`BaseDialog.h:24, 39-43`）。
  - 唯一句柄表：`GetUniqueHandel` / `AllUniqueHandles` / `IsAllDialogClosed`（`BaseDialog.h:25-27`）。
  - DPI 适配：`DPI(pixel)` / `RepositionTextBasedControls`（按列与文字宽度重排控件，`BaseDialog.h:47-71`）。
  - 派生钩子：`InitializeControls` / `GetDialogName`（`BaseDialog.h:76-78`）。
  - 工具：`SetBackgroundColor` / `SetButtonIcon` / `EnableDlgCtrl` / `IterateControls` / `GetControlRect` / `GetTextExtent`（`BaseDialog.h:17, 79-86`）。
  - 消息：`OnInitDialog` / `OnDestroy` / `OnGetMinMaxInfo` / `OnSize` / `DoModal` / `OnEraseBkgnd` / `OnCtlColor`（`BaseDialog.h:91-98`）。
- **`ColorSettingListCtrl.h` / `.cpp`** — `CColorSettingListCtrl : public CListCtrl`（`ColorSettingListCtrl.h:4-22`），通过 `OnNMCustomdraw` 给特定 (row, col) 单元格上色；`SetItemColor` / `GetItemColor` / `SetDrawItemRangMargin`。

## 异常 / 调试 / 动态库加载

- **`Crashtool.h` / `.cpp`** — `CRASHREPORT::StartCrashReport()`（`Crashtool.h:3-13`）：启用 dump 模式，崩溃时把 minidump 写到 `%LOCALAPPDATA%\Temp`，文件名形如 `yyyyMMddhhmmss_<exename>.exe.dmp`。
- **`HResultException.h` / `.cpp`** — `CHResultException : std::runtime_error`（`HResultException.h:26-42`）保存 `HRESULT` 与 `IErrorInfo`，提供 `GetError` / `HasError` / `GetHResult`。`FunctionChecker::CheckLibraryExist` / `CheckFunctionExist`（`HResultException.h:22-23`）。`ThrowIfFailed` 与模板版本（`HResultException.h:46-53`）。`LogHResultException`（`HResultException.h:55`）。`RELEASE_COM` 宏（`HResultException.h:11-18`）和 `TRAFFICMONITOR_STR*` 字符串化宏（`HResultException.h:5-9`）。
- **`DllFunctions.h` / `.cpp`** — `CDllFunction<FunctionPointer>` 模板（`DllFunctions.h:9-102`），构造时 `LoadLibrary` + `GetProcAddress`，析构 `FreeLibrary`；未取到函数指针时也会释放 DLL；`HasValue()` 指示是否可用。特化版本 `CDllFunction<R (*)(Args...)>`（`DllFunctions.h:64-102`）提供调用运算符。`CDllFunctions`（`DllFunctions.h:106-123`）封装对 `GetDpiForMonitor`（Shcore.dll）、`D3DCompile`、`DCompositionCreateDevice`、`CreateDXGIFactory2` 的延迟加载（静态 `CDllFunction` 成员，`DllFunctions.h:114-116`）。
- **`Test.h` / `.cpp`** — `CTest::Test` / `CTest::TestCommand`（`Test.h:2-10`），单元/集成测试入口。

## 边界说明

- `Nullable.hpp` 提供的 `CLazyConstructable` 在主项目里被复用为 `CTrafficMonitorApp::m_d2d_taskbar_draw_common_support`（`TrafficMonitor/TrafficMonitor.h:108`），属于"按需初始化的全局依赖"，本文件仅作为基础设施示例提及，业务用法归其他主题文档。
- `BaseDialog` 的 `RepositionTextBasedControls` / `InitializeControls` 是对话框布局约定，被各 `*SettingsDlg` 使用，本文件不展开每个具体对话框。
- 各绘制后端文件（`WIC` / `D2D1Support` / `D3D10Support1` / `Dxgi1Support2` / `DCompositionSupport` / `Image2DEffect`）的"选优/回退策略"由 `render-api.md` 主题文档描述。
- 渲染骨架、皮肤解析、设置持久化的具体业务用法分别在 `render-api.md` / 皮肤 / 设置主题文档。
