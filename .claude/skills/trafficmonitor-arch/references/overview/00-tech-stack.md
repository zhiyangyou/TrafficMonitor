# 00 · 技术栈与构建系统

> 范围：仓库根目录、3 个 vcxproj、2 个 sln、与编译相关的所有宏与依赖。
> 仅记录源码实际状态，不写趋势或建议。

## 1.1 解决方案与项目

仓库根目录提供两份 sln 解决方案文件，对应两种发布形态：

- `TrafficMonitor.sln:6-15` —— 包含全部 3 个项目：`TrafficMonitor`（主程序）、`OpenHardwareMonitorApi`（温控/硬件监控 DLL）、`PluginDemo`（插件 demo）。
- `TrafficMonitor_Lite.sln:6-8` —— 仅包含 `TrafficMonitor` + `PluginDemo`，Lite 版不链接 `OpenHardwareMonitorApi`，通过 `WITHOUT_TEMPERATURE` 宏剥离温度相关代码。

每个项目对一个 GUID：

| 项目 | GUID | 角色 |
| --- | --- | --- |
| TrafficMonitor | `{09483BED-B1E9-4827-8120-A18302C84AA8}` | MFC Application，主 EXE |
| OpenHardwareMonitorApi | `{C0A42F4A-ABB3-4575-B4D5-CEDD8379AC26}` | C++/CLI DynamicLibrary，封装 LibreHardwareMonitor |
| PluginDemo | `{D1CA3ECC-DC32-445A-B734-C4DB08D4BA34}` | MFC DynamicLibrary，演示 `ITMPlugin`/`IPluginItem` 实现 |

`TrafficMonitor.sln:7-9` 显式声明 TrafficMonitor 依赖 OpenHardwareMonitorApi（PostProject 段），Lib 链接顺序由此强制。

## 1.2 平台与配置矩阵

`TrafficMonitor.sln:16-29` 与 `TrafficMonitor.vcxproj:4-52` 定义了 12 种 (Configuration × Platform) 组合：

- **Configuration**：`Debug`、`Release`、`Debug (lite)`、`Release (lite)`。
- **Platform**：`Win32`（即 x86）、`x64`、`ARM64EC`。

Lite 与非 Lite 的差异在 `TrafficMonitor.vcxproj` 中由宏 `WITHOUT_TEMPERATURE` 控制（见 `TrafficMonitor.vcxproj:321,438,476,560,689,731`），Win32 配置使用 `WIN32` 宏，x64/ARM64EC 配置文件不再定义 `WIN32`。

`TrafficMonitor.vcxproj:56-58` 指定 `WindowsTargetPlatformVersion=10.0`、`Keyword=MFCProj`、`RootNamespace=TrafficMonitor`。

`TrafficMonitor.vcxproj:63,77,93,100,106,113` 等条目固定使用 `PlatformToolset=v143`（Visual Studio 2022）+ `CharacterSet=Unicode` + `UseOfMfc=Dynamic`。

## 1.3 编译器/链接器关键参数

- C++ 标准：`LanguageStandard=stdcpp20`（见 `TrafficMonitor.vcxproj:285,325,364,402,441,479,520,564,606,648,691,733`）。
- 警告：`WarningLevel=Level3`，`SDLCheck=true`，`TreatWarningAsError=false`。
- 优化：Debug 关闭、Release 启用 `MaxSpeed`/`FunctionLevelLinking`/`IntrinsicFunctions`（`TrafficMonitor.vcxproj:511-520`）。
- Release 启用 `WholeProgramOptimization` 与 `/LTCG`，`Link` 段启用 `EnableCOMDATFolding`、`OptimizeReferences`（`TrafficMonitor.vcxproj:78,122,131,140,148`）。
- 链接器附加依赖（Release|x64 为例，`TrafficMonitor.vcxproj:612`）：`OpenHardwareMonitorApi.lib;%(AdditionalDependencies);pdh.lib;Powrprof.lib;Dwmapi.lib`。
- `DelayLoadDLLs`（按平台略有差异，`TrafficMonitor.vcxproj:528,571,614,657,698,740`）：非 Lite 与 Lite 几乎一致，仅在 ARM64EC 下精简为只 delay-load `powrprof.dll`；`api-ms-win-core-winrt-error-l1-1-1.dll`、`api-ms-win-core-winrt-l1-1-0.dll` 仅在需要 WinRT 时被引用。
- `UACExecutionLevel=RequireAdministrator`（`TrafficMonitor.vcxproj:290,368,407,527,613,657`）—— 仅非 Lite Debug/Release 提升。
- 预编译头：所有源文件以 `pch.h` 走 `stdafx.cpp` 创建，第三方 `tinyxml2\tinyxml2.cpp` 在所有配置下显式 `NotUsing` 预编译头（`TrafficMonitor.vcxproj:943-981`）。
- 资源编译：`Culture=0x0804`（中文 PRC，见 `TrafficMonitor.vcxproj:299,338,378,417,455,494,536,574,615,652,692,729`）。
- PreBuild 步骤执行 `print_compile_time.bat`（`TrafficMonitor.vcxproj:304,343,382,421,458,496,540,583,621,661,706,753`）生成 `compile_time.txt`。

## 1.4 关键预处理宏

| 宏 | 定义位置 | 含义 |
| --- | --- | --- |
| `_DEBUG` / `NDEBUG` | vcxproj 各 ItemDefinitionGroup | Debug / Release 切换 |
| `WIN32` | 仅 Win32 配置（`TrafficMonitor.vcxproj:281,321,516,560`） | x86 平台标识 |
| `_WINDOWS` | 所有配置（`TrafficMonitor.vcxproj:281,360,399,438,476,516,560,603,646,689,731`） | Windows GUI 子系统 |
| `WITHOUT_TEMPERATURE` | 仅 Lite 配置（`TrafficMonitor.vcxproj:321,438,476,560,689,731`） | 关闭温度监控/相关 UI 入口 |
| `DISABLE_WINDOWS_WEB_EXPERIENCE_DETECTOR` | 条件编译（`TrafficMonitor.cpp:17-19`） | 关闭 WinRT/Windows Web Experience 检测 |
| `OPENHARDWAREMONITOR_EXPORTS` | OpenHardwareMonitorApi vcxproj | 启用 DLL 符号导出 |
| `MONITOR_TIME_SPAN_MIN/MAX` | `TrafficMonitor/CommonData.h:404-405` | 监控时间间隔合法范围（200/30000 ms） |
| `TASKBAR_DEFAULT_STYLE_NUM` / `TASKBAR_DEFAULT_LIGHT_STYLE_INDEX` | `TrafficMonitor/TaskbarDefaultStyle.h:4-5` | 任务栏预设方案数量与浅色默认索引 |

## 1.5 外部依赖一览

### 1.5.1 Win32 / 系统层
- **MFC / ATL**：MFCProj 模板（`TrafficMonitor.vcxproj:57`，`UseOfMfc=Dynamic`）。
- **Win32 GDI / GDI+**：GDI 全部绘图 + `GdiplusStartup/GdiplusShutdown`（`TrafficMonitor.cpp:1093-1095,1383-1385`）。
- **PDH（Performance Data Helper）**：`pdh.lib` 链路静态连接（`TrafficMonitor.vcxproj:289,329,367,406,445,483,526,570,612,655,698,740`）；`PdhHardwareQuery/` 子目录封装 `CCPUUsage`、`CPdhGPUUsage`、`CPdhDiskUsage`、`CPdhCpuFreq`，基类 `CPdhQuery`（`PdhHardwareQuery/PdhQuery.h:5-26`）。
- **Iphlpapi**：`TrafficMonitorDlg.h:6` 显式 `pragma comment(lib, "iphlpapi.lib")` —— 用于 `GetIfTable` 取网卡流量。
- **Powrprof**：`Powrprof.lib` + `powrprof.dll` delay-load（`TrafficMonitor.vcxproj:291,330,369,408,446,484,528,571,614,657,699,741`）。
- **DWM**：`Dwmapi.lib`（`TrafficMonitor.vcxproj:289,329,367,406,445,483,526,570,612,655,698,740`）。
- **WinRT (Windows.Web)**：仅 `WITHOUT_TEMPERATURE` 未定义且 `DISABLE_WINDOWS_WEB_EXPERIENCE_DETECTOR` 未定义时引用（`TrafficMonitor.cpp:17-19,50-53`）—— `WindowsWebExperienceDetector` 通过 WinRT API 查询 Web Experience Pack 是否安装。
- **Shell / Shell_NotifyIcon / FindWindow / RegisterWindowMessage**：`NOTIFYICONDATA` 与任务栏重启消息（`TrafficMonitorDlg.cpp:31` 注册 `TaskbarCreated`）。
- **WSH/WMI/Task Scheduler**：通过 `auto_start_helper.cpp` 的 `ITaskService` 等 API 提供任务计划自启动（`TrafficMonitor/auto_start_helper.cpp`，与 `TrafficMonitor.cpp:728-739` 协同）。

### 1.5.2 DirectX / 2D 渲染管线
- **D2D1 / D3D10 / DComposition / DXGI / DWrite**：`TrafficMonitor/TaskBarDlgDrawCommon.h:11-15` 与 `D2D1Support.h`、`D3D10Support1.h`、`DCompositionSupport.h`、`Dxgi1Support2.h`、`Image2DEffect.h` —— 在 `CTaskBarDlgDrawCommonSupport` 内组合（`TaskBarDlgDrawCommon.h:126-154`），由 `theApp.m_d2d_taskbar_draw_common_support` 懒构造（`TrafficMonitor.h:108`）。
- **WIC**：`TrafficMonitor/WIC.h/.cpp` 提供 Windows Imaging Component 辅助（皮肤 PNG 解码等）。
- **UpdateLayeredWindowIndirect**：`CTaskBarDlg::ShowInfo` 中通过 D2D1/DComposition 路径将合成结果提交到分层窗口（`TaskBarDlg.cpp:60-90` 处理错误回落到 GDI）。

### 1.5.3 第三方（vendored）
- **tinyxml2**：vendored 于 `TrafficMonitor/tinyxml2/tinyxml2.{h,cpp}`，由 `TinyXml2Helper.h` 包装（`TrafficMonitor.vcxproj:862-862,969-982`）。
- **LibreHardwareMonitor**：在 `OpenHardwareMonitorApi/` 子项目以 C++/CLI 包装（`OpenHardwareMonitorApi.vcxproj:31` `TargetFrameworkVersion=v4.7.2`、`CLRSupport=true`），底层 DLL `LibreHardwareMonitorLib.dll` 放置于该子项目根目录（`OpenHardwareMonitorApi/LibreHardwareMonitorLib.dll`）。
- **OpenHardwareMonitorApi 头文件**：位于 `include/OpenHardwareMonitor/OpenHardwareMonitorApi.h`（被 `TrafficMonitor.h:17` 引用），接口由非托管 C++ 在该头中定义，由 `COpenHardwareMonitor` 实现（`OpenHardwareMonitorApi/OpenHardwareMonitorImp.h:14-64`）。

### 1.5.4 IHV / OEM
- 仓库本身不直接调用 IHV SDK；通过 LibreHardwareMonitor 间接获得 Intel/AMD/NVIDIA 的传感器信息（`OpenHardwareMonitorImp.cpp` 内 `using namespace LibreHardwareMonitor::Hardware;` 见 `OpenHardwareMonitorImp.h:10`）。

### 1.5.5 插件接口
- `include/PluginInterface.h` 定义纯 C++ 抽象接口 `ITMPlugin` / `IPluginItem` / `ITrafficMonitor`（`include/PluginInterface.h:9,158,329`）；被主程序 `TrafficMonitor.h:24` 与 `OpenHardwareMonitorApi.vcxproj` 之外的插件作者直接使用。

## 1.6 运行时与部署产物

`TrafficMonitor.vcxproj:195-275` 给出各配置的 `OutDir`：

- Win32：`$(SolutionDir)Bin\$(Configuration)\`（如 `Bin\Release\`）。
- x64/ARM64EC：`$(SolutionDir)Bin\$(Platform)\$(Configuration)\`（如 `Bin\x64\Release\`、`Bin\ARM64EC\Release\`）。

`LibraryPath` 顺序为 `$(SolutionDir)lib\;$(OutDir);$(LibraryPath)`，ARM64EC 额外追加 `$(SolutionDir)Bin\x64\$(Configuration)\` 以引用 x64 编译产物（`TrafficMonitor.vcxproj:218,232,260,274`）—— 这是 ARM64EC 仿真执行所必需。

非资源文件 `LICENSE`、`LICENSE_CN`、`language\*.ini`、`res\TrafficMonitor.rc2` 全部以 `None Include` 加入项目（`TrafficMonitor.vcxproj:997-1003`），保证部署到 OutDir 时随包拷贝。

## 1.7 与本节相关的关键文件索引

- 解决方案：`TrafficMonitor.sln`、`TrafficMonitor_Lite.sln`。
- 项目文件：`TrafficMonitor/TrafficMonitor.vcxproj`、`OpenHardwareMonitorApi/OpenHardwareMonitorApi.vcxproj`、`PluginDemo/PluginDemo.vcxproj`。
- 主入口：`TrafficMonitor/TrafficMonitor.cpp:915-1131`（`InitInstance`）。
- 编译期宏集合见 `TrafficMonitor/CommonData.h:404-405`、`TrafficMonitor/TaskbarDefaultStyle.h:4-5`。
