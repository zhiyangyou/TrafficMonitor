# OpenHardwareMonitorApi 项目概述

## 项目目标

`OpenHardwareMonitorApi` 是一个 C++/CLI DLL（`OpenHardwareMonitorApi.dll`），位于独立的子项目 `OpenHardwareMonitorApi/` 中。它把 `LibreHardwareMonitorLib.dll`（随包发布的第三方 .NET 程序集，见 `OpenHardwareMonitorApi/LibreHardwareMonitorLib.dll` 及同目录 `LibreHardwareMonitorLib.xml`）包装成 C++ 风格接口 `IOpenHardwareMonitor`，供主程序 `TrafficMonitor` 调用，从而获得 CPU/GPU/HDD/主板的温度、频率、占用率等数据。

入口文件：
- `OpenHardwareMonitorApi/OpenHardwareMonitorImp.cpp:29-47` 定义了 DLL 工厂函数 `CreateInstance()` 与 `GetErrorMessage()`。
- `OpenHardwareMonitorApi/ReadMe.txt:1-3` 说明该项目由 AppWizard 生成的 DLL 模板改造而来。

## 接口层（include/）

### `include/OpenHardwareMonitor/OpenHardwareMonitorApi.h`

声明对外的纯虚接口 `IOpenHardwareMonitor`：
- `void GetHardwareInfo()` — 触发一次硬件信息采集（行 12）。
- 单值读出器（行 13-19）：`CpuTemperature()` / `GpuTemperature()` / `HDDTemperature()` / `MainboardTemperature()` / `GpuUsage()` / `CpuFreq()` / `CpuUsage()`。
- map 读出器（行 20-22）：`AllHDDTemperature()` / `AllCpuTemperature()` / `AllHDDUsage()`，key 是硬件名称，value 是读数。
- 启用开关（行 24-27）：`SetCpuEnable()` / `SetGpuEnable()` / `SetHddEnable()` / `SetMainboardEnable()`，对应设置是否让 LibreHardwareMonitor 采集某种硬件。
- 工厂函数（行 30）：`std::shared_ptr<IOpenHardwareMonitor> CreateInstance()`。
- 错误消息导出（行 31）：`GetErrorMessage()`，返回 `CreateInstance()` 失败时捕获的异常文本。

### `include/OpenHardwareMonitor/OpenHardwareMonitorGlobal.h`

简单的导出宏 `OPENHARDWAREMONITOR_API`，由 `OPENHARDWAREMONITOR_EXPORTS` 切换 `__declspec(dllexport)` / `__declspec(dllimport)`（行 3-7）。

## 实现层（OpenHardwareMonitorApi/）

### `OpenHardwareMonitorImp.h` / `.cpp`

定义实现类 `COpenHardwareMonitor`（继承 `IOpenHardwareMonitor`）与托管单例 `MonitorGlobal`（`ref class`）：

- **私有成员变量**（`.h:48-63`）：分别记录三类 GPU（nvidia / ati / intel）的温度与占用、CPU 温度/频率/占用、主板温度、硬盘温度；以及三个 `std::map<wstring, float>` 存放所有 CPU / 硬盘的温度与占用。
- **`ResetAllValues()`**（`.cpp:290-305`）：把全部成员清零为 -1 或空。
- **`InsertValueToMap()`**（`.cpp:307-332`）：把 (key, value) 插入 map；若 key 已存在，则在 key 末尾追加 `" #n"`（n 递增）以解决多盘重名问题。
- **私有读取函数**（`.cpp`）：
  - `GetHardwareTemperature()` — 通用温度读取，优先匹配 `Core Average` / `GPU Core` 等核心传感器，否则对所有温度传感器取均值，必要时递归到 `SubHardware`（`.cpp:163-212`）。
  - `GetCpuTemperature()` — 遍历 CPU 传感器，把每个核心名称与温度写入 `m_all_cpu_temperature`，返回所有核心均值（`.cpp:214-237`）。
  - `GetCPUFreq()` — 收集所有 `SensorType::Clock` 传感器值（去掉 `Bus Speed`），求和再除以数量并换算为 GHz（`.cpp:129-144`）。
  - `GetCpuUsage()` — 找到第一个非 `CPU Total` 的 `SensorType::Load` 传感器（`.cpp:146-161`）。
  - `GetGpuUsage()` — 优先取名为 `GPU Core` 的 Load 传感器，否则取所有 Load 传感器最大值（`.cpp:239-261`）。
  - `GetHddUsage()` — 取名为 `Total Activity` 的 Load 传感器（`.cpp:263-278`）。
- **`GetHardwareInfo()`**（`.cpp:334-400`）：`ResetAllValues()` 后用 `MonitorGlobal::Instance()->updateVisitor` 走访 `Computer`，然后按 `HardwareType` 分发到上述读取函数，填齐各成员。
- **GPU 选取**（`.cpp:54-62, 74-82`）：`GpuTemperature()` / `GpuUsage()` 按 nvidia → ati → intel 顺序返回第一个有效值。
- **启用开关**（`.cpp:109-127`）：四个 `SetXxxEnable()` 直接修改 `MonitorGlobal::Instance()->computer->IsXxxEnabled`。
- **`MonitorGlobal`**（`.h:67-90`，`.cpp:403-423`）：托管单例，持有 `Computer^` 与 `UpdateVisitor^`；`Init()` 创建并 `Open()`，`UnInit()` 调用 `Close()`。注释明确说明由于 `COpenHardwareMonitor` 是非托管类，不能直接持有托管成员，所以单独用这个 ref class 托管 `Computer` 与 `Visitor`。
- **`ClrStringToStdWstring()`**（`.cpp:13-26`）：将 CLR `System::String^` 转换为 `std::wstring`。
- **错误信息**（`.cpp:10, 38-40, 44-47, 337, 396-399`）：`error_message` 静态变量在 `CreateInstance()` 和 `GetHardwareInfo()` 捕获 `System::Exception^` 时填入，`GetErrorMessage()` 读出。

### `UpdateVisitor.h` / `.cpp`

实现 `IVisitor`（LibreHardwareMonitor 的访问者接口）：
- `VisitComputer()`（`.cpp:6-9`）：调用 `computer->Traverse(this)` 触发对硬件树的遍历。
- `VisitHardware()`（`.cpp:11-18`）：调用 `hardware->Update()` 刷新传感器值，并递归访问所有 `SubHardware`。
- `VisitSensor()` / `VisitParameter()`（`.cpp:20-26`）：空实现。

注释见 `UpdateVisitor.h:1-4`，设计目的是强制让树上的每个硬件节点都执行一次 `Update()`，确保随后 `COpenHardwareMonitor::GetHardwareInfo()` 读取的 `Value` 是新值。

### `Stdafx.cpp` / `Stdafx.h` / `app.rc` / `resource.h`

项目脚手架：
- `Stdafx.h` 是空头文件（被 CLI 项目当作预编译头占位，`.h:1-8`）。
- `Stdafx.cpp` 仅 `#include "stdafx.h"`（`.cpp:1-6`）。
- `app.rc` 是 MFC 风格的资源脚本，包含版本信息（`FileVersion` / `ProductVersion` = 1.0.0.3，CompanyName = `By ZhongYang`）；UTF-16 LE 编码。
- `resource.h` 仅由 `app.rc` 引用，定义资源 ID（未直接打开阅读）。
- `OpenHardwareMonitorApi.vcxproj` / `.filters` 由 IDE 生成，描述项目文件归属与编译配置。

## 主程序怎么使用

主程序项目编译时通过预处理宏 `WITHOUT_TEMPERATURE` 控制是否接入该 DLL。`TrafficMonitor/TrafficMonitor.vcxproj` 在 Debug / Release 的 Win32 与 x64 配置中都定义了 `WITHOUT_TEMPERATURE`（行 321, 438, 476, 560, 689, 731），所以当前主项目源码中相关代码都被 `#ifndef WITHOUT_TEMPERATURE` 包裹，处于禁用状态；接口本身仍然在子项目中被实现。

在 `WITHOUT_TEMPERATURE` 未定义时（即历史/另编版本中）：

- **成员指针**（`TrafficMonitor/TrafficMonitor.h:101-104`）：
  ```cpp
  #ifndef WITHOUT_TEMPERATURE
  std::shared_ptr<OpenHardwareMonitorApi::IOpenHardwareMonitor> m_pMonitor{};
  #endif
  ```
- **线程同步**（`TrafficMonitor/TrafficMonitor.h:106`）：`CCriticalSection m_minitor_lib_critical;` 用于串行化对 `m_pMonitor` 的访问。
- **初始化**（`TrafficMonitor/TrafficMonitor.cpp:621-634`）：`CTrafficMonitorApp::InitOpenHardwareMonitorLibThreadFunc()` 加锁后调用 `OpenHardwareMonitorApi::CreateInstance()`，失败时弹 `GetErrorMessage()`，然后调用 `theApp.UpdateOpenHardwareMonitorEnableState()` 同步启用位。
- **启用位同步**（`TrafficMonitor/TrafficMonitor.cpp:1142-1152`）：读取 `m_general_data.IsHardwareEnable()` 后调用 `m_pMonitor->SetXxxEnable()`。
- **周期性采集**（`TrafficMonitor/TrafficMonitorDlg.cpp:1409-1490`）：`CTrafficMonitorDlg::UpdateOpenHardwareMonitor()` 加锁后用 SEH（`__try` / `__except`）调 `m_pMonitor->GetHardwareInfo()`，随后读出 GPU / MBD 温度、CPU 频率、CPU / HDD map 数据；找不到所选硬件时会回退到 map 第一个键。
- **释放**（`TrafficMonitor/TrafficMonitorDlg.cpp:806-810`）：在设置页"硬件监控"项被关闭时，重置 `m_pMonitor.reset()`，依赖 `shared_ptr` 在析构时调用 `COpenHardwareMonitor::~COpenHardwareMonitor()`（`OpenHardwareMonitorImp.cpp:285-288`）触发 `MonitorGlobal::UnInit()`。

UI 侧通过 `IsTemperatureNeeded()` 判定当前是否需要温度显示（`TrafficMonitorDlg.cpp:1411`），并由 `AboutDlg.cpp:102-103`、`GeneralSettingsDlg.cpp:280, 388, 418`、`TaskBarDlg.cpp:757`、`UpdateHelper.cpp:73` 等多处 `#ifdef WITHOUT_TEMPERATURE` 决定是否显示"无温度监控"标识或温度相关设置项。

## 与"硬件监控"主题文档的边界

本文档只覆盖"接入层"：
- 接口形状（`IOpenHardwareMonitor`）与实现机制（`Computer` / `IVisitor` / `Update`）。
- 主程序如何持有、并发保护、初始化、释放该对象。
- 编译期开关 `WITHOUT_TEMPERATURE` 的作用位置。

不在本文档范围（由 hardware-monitoring.md 覆盖）：
- 各项监控数据的语义、UI 展示与可配置项（`DisplayItem`、温度面板、皮肤里的温度项等）。
- `GeneralSettingData::hardware_monitor_item` 的具体位含义（已在 `CommonData.h:51-57` 列出枚举 `HardwareItem` 的位定义，主题文档再行叙述）。
- LibreHardwareMonitor 内部硬件树结构细节（CPU/HDD 多盘的处理方式与重名重命名逻辑 `InsertValueToMap` 在本文件仅作为"接口行为"提及）。
