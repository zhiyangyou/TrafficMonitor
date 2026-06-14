# 硬件监控

本文件描述 TrafficMonitor 主程序如何把 LibreHardwareMonitor（通过 `OpenHardwareMonitorApi` 项目的非托管 C++/CLI 封装）挂进自己的采集周期，以及标准版与 Lite 版之间通过 `WITHOUT_TEMPERATURE` 宏切开的具体边界。

## 职责

- 初始化并持有一个 `IOpenHardwareMonitor` 实例；
- 周期性地把 CPU 温度 / 显卡温度 / 主板温度 / 硬盘温度 / CPU 频率 / 显卡利用率 / 硬盘利用率 写入 `CTrafficMonitorApp` 上的公共字段；
- 把用户在"常规设置"里勾选的硬件子集映射到底层 LibreHardwareMonitor 的 `IsCpuEnabled` / `IsGpuEnabled` / `IsStorageEnabled` / `IsMotherboardEnabled`；
- Lite 版（无 `WITHOUT_TEMPERATURE`）下完全剔除这套设施。

## 关键类 / 函数

### 1. `WITHOUT_TEMPERATURE` 宏如何切开标准版 vs Lite 版

在 `TrafficMonitor/TrafficMonitor.vcxproj` 的 ItemDefinitionGroup 中，凡 Configuration 名带 `(lite)` 的：

- `Debug (lite)|Win32`：`PreprocessorDefinitions=WIN32;_WINDOWS;_DEBUG;%(PreprocessorDefinitions);WITHOUT_TEMPERATURE`（`TrafficMonitor.vcxproj:321`）
- `Debug (lite)|x64`：同上 360 行附近
- `Release (lite)|Win32`：560 行附近
- `Release (lite)|x64`：646 行附近
- `Debug (lite)|ARM64EC` / `Release (lite)|ARM64EC`：438 / 689 行附近

不带 `(lite)` 的标准版 `Release|Win32`（`TrafficMonitor.vcxproj:516`）与 `Release|x64`（603 行附近）只保留 `WIN32;_WINDOWS;NDEBUG;%(PreprocessorDefinitions)`，不带 `WITHOUT_TEMPERATURE`。

`TrafficMonitor.sln:17-90` 共有 6 套 Lite 配置映射到标准版的 `Debug` / `Release`。

附加的链接差异：Lite 版 `Link.AdditionalDependencies` 不含 `OpenHardwareMonitorApi.lib`，仅保留 `pdh.lib;Powrprof.lib;Dwmapi.lib`（参见 `TrafficMonitor.vcxproj:329` 与非 Lite 版 `289/367/406/526/612/655`）。

宏的代码作用面：
- `TrafficMonitor.h:101-104`：`std::shared_ptr<IOpenHardwareMonitor> m_pMonitor{};` 整块被 `#ifndef WITHOUT_TEMPERATURE ... #endif` 包起来。
- `TrafficMonitor.cpp:623-633`：`InitOpenHardwareMonitorLibThreadFunc` 函数体只在非 Lite 下编译，Lite 下函数体只剩 `return 0;`。
- `TrafficMonitor.cpp:1135-1138`：`InitOpenHardwareLibInThread` 中 `AfxBeginThread(InitOpenHardwareMonitorLibThreadFunc, NULL);` 也只在非 Lite 下生效。
- `TrafficMonitor.cpp:1141-1153`：`UpdateOpenHardwareMonitorEnableState` 整段被 `#ifndef WITHOUT_TEMPERATURE` 包住。
- `TrafficMonitor.cpp:771-773`：`GetSystemInfoString` 中 Lite 版额外追加 ` (Lite)` 标识。
- `TrafficMonitorDlg.cpp:802-825`：选项设置中关闭硬件监控 / 重新启用硬件监控的逻辑整体包在 `#ifndef WITHOUT_TEMPERATURE` 内。
- `TrafficMonitorDlg.cpp:1344-1497`：Lite 版用 `bool lite_version = true;` 让 GPU/HDD 利用率走 PDH 路径，温度段整段用 `#ifndef WITHOUT_TEMPERATURE ... #endif` 包住。
- `TrafficMonitorDlg.cpp:1409` `#ifndef WITHOUT_TEMPERATURE` 保护开始；`1497` 结束。

### 2. `OpenHardwareMonitorApi` 项目

源代码在 `OpenHardwareMonitorApi/` 目录下，对应 `OpenHardwareMonitorApi.vcxproj`。

#### 接口定义
`include/OpenHardwareMonitor/OpenHardwareMonitorApi.h:9-28`：

```cpp
class IOpenHardwareMonitor {
public:
    virtual void GetHardwareInfo() = 0;
    virtual float CpuTemperature() = 0;
    virtual float GpuTemperature() = 0;
    virtual float HDDTemperature() = 0;
    virtual float MainboardTemperature() = 0;
    virtual float GpuUsage() = 0;
    virtual float CpuFreq() = 0;
    virtual float CpuUsage() = 0;
    virtual const std::map<std::wstring, float>& AllHDDTemperature() = 0;
    virtual const std::map<std::wstring, float>& AllCpuTemperature() = 0;
    virtual const std::map<std::wstring, float>& AllHDDUsage() = 0;
    virtual void SetCpuEnable(bool enable) = 0;
    virtual void SetGpuEnable(bool enable) = 0;
    virtual void SetHddEnable(bool enable) = 0;
    virtual void SetMainboardEnable(bool enable) = 0;
};
OPENHARDWAREMONITOR_API std::shared_ptr<IOpenHardwareMonitor> CreateInstance();
OPENHARDWAREMONITOR_API std::wstring GetErrorMessage();
```

接口有两个 export 函数：`CreateInstance()` 工厂返回 `shared_ptr<IOpenHardwareMonitor>`；`GetErrorMessage()` 返回最近一次构造时捕获的 CLR 异常文本。

#### `COpenHardwareMonitor` 实现
`OpenHardwareMonitorApi/OpenHardwareMonitorImp.h:14-64` 声明类，`OpenHardwareMonitorImp.cpp` 是实现。

字段（`OpenHardwareMonitorImp.h:48-63`）：按厂商分别保留 `m_gpu_nvidia_temperature` / `m_gpu_ati_temperature` / `m_gpu_intel_temperature`，`m_all_hdd_temperature` / `m_all_cpu_temperature` / `m_all_hdd_usage` 是按硬件名索引的 `std::map<std::wstring, float>`。

构造函数（`OpenHardwareMonitorImp.cpp:280-283`）调用 `ResetAllValues()`（同文件 290-305 行）：所有数值字段置 `-1`，所有 map `clear`。析构（285-288 行）调 `MonitorGlobal::Instance()->UnInit()`。

`GetHardwareInfo()`（同文件 334-400）：
1. `ResetAllValues()` + 清空 `error_message`；
2. `computer->Accept(updateVisitor)` 触发 LibreHardwareMonitor 的 `UpdateVisitor`（`OpenHardwareMonitorApi/UpdateVisitor.cpp:7-9` 即 `VisitComputer` 调 `computer->Traverse(this)`）；
3. `VisitHardware`（`UpdateVisitor.cpp:11-18`）对每个 `IHardware` 调 `hardware->Update()`，并递归访问 `SubHardware`；
4. 主循环遍历 `computer->Hardware`，按 `HardwareType` 分发：
   - `Cpu` → `GetCpuTemperature` / `GetCPUFreq` / `GetCpuUsage`（`OpenHardwareMonitorImp.cpp:347-353`）；
   - `GpuNvidia` / `GpuAmd` / `GpuIntel` → 各自的温度 + 利用率，按厂商填入 `m_gpu_*_temperature` / `m_gpu_*_usage`（355-372 行）；
   - `Storage` → 写入 `m_all_hdd_temperature` / `m_all_hdd_usage`，并把首块盘的温度写入 `m_hdd_temperature`（373-386 行）；
   - `Motherboard` → `m_main_board_temperature`（387-390 行）。

各 Get 方法关键路径：
- `GetHardwareTemperature`（`OpenHardwareMonitorImp.cpp:163-212`）：先尝试 `Core Average`（CPU）/ `GPU Core`（显卡）作为精确名；找不到就取所有温度传感器的平均值；硬件本身没温度传感器就递归到 `SubHardware`。
- `GetCpuTemperature`（`OpenHardwareMonitorImp.cpp:214-237`）：清空 `m_all_cpu_temperature`，把所有 `SensorType::Temperature` 写到 map，再算平均写入 `temperature` 参数。
- `GetGpuUsage`（`OpenHardwareMonitorImp.cpp:239-261`）：优先取 `GPU Core`，否则取所有 Load 传感器的最大值。
- `GetHddUsage`（`OpenHardwareMonitorImp.cpp:263-278`）：取名为 `Total Activity` 的 Load 传感器。
- `GetCPUFreq`（`OpenHardwareMonitorImp.cpp:129-144`）：把所有 `Clock` 传感器求和除以数量再除以 1000（GHz），过滤 `Bus Speed`。
- `GetCpuUsage`（`OpenHardwareMonitorImp.cpp:146-161`）：取第一个不是 `CPU Total` 的 Load 传感器。
- `InsertValueToMap`（`OpenHardwareMonitorImp.cpp:307-332`）：处理同名硬件（重名盘），给 key 加 ` #1` / `#2` 后缀。

`GpuTemperature()`（49-62 行）：按 NVIDIA → AMD → Intel 的优先级返回第一个 ≥ 0 的厂商温度。`GpuUsage()`（74-82 行）相同策略。

#### `MonitorGlobal` 单例
`OpenHardwareMonitorImp.h:67-90`：
- `Computer^ computer`：托管 LibreHardwareMonitor 的 `Computer` 对象；
- `UpdateVisitor^ updateVisitor`：上面那个访问者；
- `Init()`（`OpenHardwareMonitorImp.cpp:413-418`）：`updateVisitor = gcnew UpdateVisitor(); computer = gcnew Computer(); computer->Open();`
- `UnInit()`（420-423 行）：`computer->Close();`

由于 `COpenHardwareMonitor` 是非托管类，不能直接持有托管成员，所以用这个 ref class 单例包住 `Computer`。

#### `UpdateVisitor`
`OpenHardwareMonitorApi/UpdateVisitor.h:7-14` / `UpdateVisitor.cpp:6-26`：实现 LibreHardwareMonitor 的 `IVisitor` 接口。`VisitComputer` 触发遍历，`VisitHardware` 调 `hardware->Update()` 并递归到 `SubHardware`。`VisitSensor` / `VisitParameter` 是空实现。

#### 工厂入口
`OpenHardwareMonitorImp.cpp:29-42` 的 `CreateInstance()`：先 `MonitorGlobal::Instance()->Init()`，再 `make_shared<COpenHardwareMonitor>()`；`try/catch (System::Exception^ e)` 捕获托管异常，把消息存进 `error_message`，返回空 `shared_ptr`。

### 3. 主程序怎么用

`CTrafficMonitorApp` 上的实例成员（`TrafficMonitor/TrafficMonitor.h:103`）：
```cpp
#ifndef WITHOUT_TEMPERATURE
    std::shared_ptr<OpenHardwareMonitorApi::IOpenHardwareMonitor> m_pMonitor{};
#endif
```

调用面集中在 `TrafficMonitor.cpp` 与 `TrafficMonitorDlg.cpp`：

#### 创建（在后台线程）
`CTrafficMonitorApp::InitOpenHardwareMonitorLibThreadFunc`（`TrafficMonitor.cpp:621-634`）：
```cpp
UINT CTrafficMonitorApp::InitOpenHardwareMonitorLibThreadFunc(LPVOID lpParam)
{
#ifndef WITHOUT_TEMPERATURE
    CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
    theApp.m_pMonitor = OpenHardwareMonitorApi::CreateInstance();
    if (theApp.m_pMonitor == nullptr)
        AfxMessageBox(OpenHardwareMonitorApi::GetErrorMessage().c_str(), MB_ICONERROR | MB_OK);
    theApp.UpdateOpenHardwareMonitorEnableState();
#endif
    return 0;
}
```

被 `CTrafficMonitorApp::InitOpenHardwareLibInThread`（`TrafficMonitor.cpp:1133-1138`）通过 `AfxBeginThread` 异步调用。Lite 版整个函数体为空。

启动时机：当选项设置里关闭过硬件监控（`m_pMonitor` 被 reset）后再启用时（`TrafficMonitorDlg.cpp:817`），或用户在皮肤 / 选项里把 `HI_CPU/HI_GPU/HI_HDD/HI_MBD` 任意一项勾上时（`IsTemperatureNeeded()` 在 `TrafficMonitorDlg.cpp:1002-1031` 中通过 `m_general_data.IsHardwareEnable` 判断）。

#### 更新启用状态
`CTrafficMonitorApp::UpdateOpenHardwareMonitorEnableState`（`TrafficMonitor.cpp:1141-1153`）：
```cpp
if (m_pMonitor != nullptr) {
    CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
    m_pMonitor->SetCpuEnable(m_general_data.IsHardwareEnable(HI_CPU));
    m_pMonitor->SetGpuEnable(m_general_data.IsHardwareEnable(HI_GPU));
    m_pMonitor->SetHddEnable(m_general_data.IsHardwareEnable(HI_HDD));
    m_pMonitor->SetMainboardEnable(m_general_data.IsHardwareEnable(HI_MBD));
}
```
底层 `SetCpuEnable` 等（`OpenHardwareMonitorImp.cpp:109-127`）直接转写到 `MonitorGlobal::Instance()->computer->IsCpuEnabled = enable;` 等。

#### 采集期使用
`CTrafficMonitorDlg::DoMonitorAcquisition`（`TrafficMonitorDlg.cpp:1409-1497`）：

```cpp
#ifndef WITHOUT_TEMPERATURE
    if (IsTemperatureNeeded() && theApp.m_pMonitor != nullptr)
    {
        CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
        CString error_info = CCommon::LoadText(IDS_HARDWARE_INFO_ACQUIRE_FAILED_ERROR);

        auto getHardwareInfo = [&]() {
            __try { theApp.m_pMonitor->GetHardwareInfo(); }
            __except (EXCEPTION_EXECUTE_HANDLER) { AfxMessageBox(error_info, MB_ICONERROR | MB_OK); }
        };
        getHardwareInfo();
        auto monitor_error_message{ OpenHardwareMonitorApi::GetErrorMessage() };
        if (!monitor_error_message.empty()) {
            AfxMessageBox(monitor_error_message.c_str(), MB_ICONERROR | MB_OK);
        }
        theApp.m_gpu_temperature         = theApp.m_pMonitor->GpuTemperature();
        theApp.m_main_board_temperature  = theApp.m_pMonitor->MainboardTemperature();
        if (!gpu_usage_acquired)         theApp.m_gpu_usage  = theApp.m_pMonitor->GpuUsage();
        if (!cpu_freq_acquired)          theApp.m_cpu_freq   = theApp.m_pMonitor->CpuFreq();
        // CPU 温度：从 AllCpuTemperature() 里按 cpu_core_name 取
        // 硬盘温度 / 利用率：从 AllHDDTemperature() / AllHDDUsage() 里按 hard_disk_name 取
    }
#endif
```

PDH 已经取得的 `gpu_usage` / `cpu_freq` 优先，OpenHardwareMonitor 只在 PDH 失败时补位。

#### 释放
选项对话框中关闭硬件监控时（`TrafficMonitorDlg.cpp:803-810`）：
```cpp
if (is_hardware_monitor_item_changed) {
    if (theApp.m_general_data.hardware_monitor_item == 0) {
        CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
        theApp.m_pMonitor.reset();
    }
    ...
}
```
`shared_ptr.reset()` 触发 `COpenHardwareMonitor` 析构 → `MonitorGlobal::Instance()->UnInit()` → `computer->Close()`。

### 4. 监控项映射（`m_general_data.hardware_monitor_item`）

`enum HardwareItem`（`TrafficMonitor/CommonData.h:51-57`）：`HI_CPU=1<<0` / `HI_GPU=1<<1` / `HI_HDD=1<<2` / `HI_MBD=1<<3`，按位 OR 存放在 `GeneralSettingData::hardware_monitor_item`（`CommonData.h:387`）。`IsHardwareEnable(item)` / `SetHardwareEnable(item, enable)`（`CommonData.h:388-398`）做位操作。

`GeneralSettingsDlg.cpp:31-43` / `390-393` / `715-754` 给出 UI 接线。

| 监控项 | 字段 | LibreHardwareMonitor 路径 |
|---|---|---|
| CPU 温度 | `m_cpu_temperature` | `AllCpuTemperature()[cpu_core_name]`，或全部均值 |
| 显卡温度 | `m_gpu_temperature` | `GpuTemperature()` (Nvidia→Amd→Intel 优先级) |
| 主板温度 | `m_main_board_temperature` | `MainboardTemperature()` |
| 硬盘温度 | `m_hdd_temperature` | `AllHDDTemperature()[hard_disk_name]` |
| CPU 频率 | `m_cpu_freq` | `CpuFreq()` 或 PDH `CPdhCpuFreq` 优先 |
| GPU 利用率 | `m_gpu_usage` | `GpuUsage()` 或 PDH `CPdhGPUUsage` 优先 |
| HDD 利用率 | `m_hdd_usage` | `AllHDDUsage()[hard_disk_name]` 或 PDH `CPdhDiskUsage` 优先 |
| CPU 利用率 | `m_cpu_usage` | （本字段走 PDH/系统时间，LibreHardwareMonitor 的 `CpuUsage()` 实现存在但未被主程序采集路径使用） |

UI 接线（GeneralSettingsDlg.cpp）：
- `m_select_cpu_combo.EnableWindow(IsHardwareEnable(HI_CPU))`（211 行）
- `IDC_CPU_CHECK` / `IDC_GPU_CHECK` / `IDC_HDD_CHECK` / `IDC_MBD_CHECK` 对应四个 checkbox（390-393 行）
- `m_data.SetHardwareEnable(HI_CPU, checked)` 等（715-754 行）

#### 硬件源选择
- CPU 核心：`m_general_data.cpu_core_name`（`CommonData.h:385`）。当值为 `IDS_AVREAGE_TEMPERATURE`（"平均温度"）时取 `m_pMonitor->CpuTemperature()`（`TrafficMonitorDlg.cpp:1444`）；否则按名字在 `AllCpuTemperature()` 中查。找不到时回退到首项并写回 `m_general_data.cpu_core_name`（1450-1457 行）。
- 硬盘：`m_general_data.hard_disk_name`（`CommonData.h:384`）。同上策略（1466-1472 行的 `AllHDDTemperature` 与 1483-1489 行的 `AllHDDUsage`）。

PDH 路径下还有独立的回退选择：CPU / HDD 的 PDH helper 找不到 `hard_disk_name` 时退化到 `_Total`，再退化到 `m_diskNames.front()`（`TrafficMonitorDlg.cpp:1375-1394`）。

### 5. 错误与降级

#### 初始化失败
`InitOpenHardwareMonitorLibThreadFunc` 中 `CreateInstance()` 抛 `System::Exception^` 时（`OpenHardwareMonitorImp.cpp:37-40`）把异常消息存进 `error_message`，返回空 `shared_ptr`；主程序随后 `AfxMessageBox(OpenHardwareMonitorApi::GetErrorMessage().c_str(), MB_ICONERROR | MB_OK)`（`TrafficMonitor.cpp:628`）。

#### 采集期崩溃
`GetHardwareInfo()` 在 SEH 层面可能崩溃（C++/CLI 边界），主程序用 `__try / __except (EXCEPTION_EXECUTE_HANDLER)` 包住，崩溃时弹 `IDS_HARDWARE_INFO_ACQUIRE_FAILED_ERROR`（`TrafficMonitorDlg.cpp:1417-1425`）。非 SEH 的托管异常会被 `OpenHardwareMonitorImp.cpp:396-399` 捕获并写入 `error_message`，主程序在 `DoMonitorAcquisition` 末尾再次检查并弹框（`TrafficMonitorDlg.cpp:1428-1432`）。

#### `-1` 哨兵语义
`COpenHardwareMonitor::ResetAllValues`（`OpenHardwareMonitorImp.cpp:290-305`）把所有数值字段置 `-1`。各字段在主程序的语义：

| 字段 | 哨兵 | UI 表现 |
|---|---|---|
| `m_cpu_temperature` / `m_gpu_temperature` / `m_hdd_temperature` / `m_main_board_temperature`（float） | `<= 0`（含 `-1`） | `CCommon::TemperatureToString` 输出 `--°C`（`Common.cpp:349-360`） |
| `m_cpu_usage` / `m_memory_usage` / `m_gpu_usage` / `m_hdd_usage`（int） | `< 0` | `CCommon::UsageToString` 输出 `--%`（`Common.cpp:362-376`） |
| `m_cpu_freq`（float） | `< 0` | `CCommon::FreqToString` 输出 `-- GHz`（`Common.cpp:378-389`） |

写入 `-1` 的来源：
- `ResetAllValues`：`OpenHardwareMonitorImp.cpp:290-305`；
- 找不到数据源：`TrafficMonitorDlg.cpp:1369`（GPU PDH 失败）、`1398`（HDD PDH 失败）、`1461`（CPU 温度 map 为空）、`1476`（HDD 温度 map 为空）、`1493`（HDD 利用率 map 为空）。

UI 通过 `DisplayItem::GetItemValueText`（`DisplayItem.cpp:185-276`）调用上述字符串格式化函数，因此 `-1` → `--` 是统一的降级路径，对主窗口和任务栏窗口都生效。

#### 切换降级
选项关闭 `hardware_monitor_item` 任一位时（`TrafficMonitorDlg.cpp:803-825`）：
- 全关 → `m_pMonitor.reset()` 释放；
- 局部关 → `UpdateOpenHardwareMonitorEnableState()` 把对应 `IsCpuEnabled` 等置 false，LibreHardwareMonitor 内部停止访问该硬件传感器；
- 全关后再开 → `InitOpenHardwareLibInThread()` 异步重建 `m_pMonitor`。

#### `m_pMonitor == nullptr` 的兜底
`TrafficMonitorDlg.cpp:1411` 同时检查 `IsTemperatureNeeded() && theApp.m_pMonitor != nullptr`，任一不满足就跳过整段硬件采集，温度字段保持上次的值（启动时默认 `-1`）。

## 调用链

```
[启动 / 启用硬件监控]
  AfxBeginThread(InitOpenHardwareMonitorLibThreadFunc)
    └── CSingleLock(&m_minitor_lib_critical)
          ├── OpenHardwareMonitorApi::CreateInstance()
          │     ├── MonitorGlobal::Instance()->Init()
          │     │     ├── new UpdateVisitor()
          │     │     └── new Computer() → Computer.Open()
          │     └── new COpenHardwareMonitor (字段 reset 为 -1)
          └── UpdateOpenHardwareMonitorEnableState()
                └── m_pMonitor->Set{Cpu,Gpu,Hdd,Mainboard}Enable
                      └── computer->Is{Cpu,Gpu,Storage,Motherboard}Enabled

[采集周期 DoMonitorAcquisition]
  if (IsTemperatureNeeded && m_pMonitor != nullptr):
    CSingleLock(&m_minitor_lib_critical)
      m_pMonitor->GetHardwareInfo()
        ├── computer->Accept(updateVisitor)
        │     └── VisitComputer → computer->Traverse(this)
        │           └── VisitHardware → hardware->Update() + 递归 SubHardware
        └── switch (HardwareType):
              Cpu              → GetCpuTemperature / GetCPUFreq / GetCpuUsage
              GpuNvidia/Amd/Intel → GetHardwareTemperature + GetGpuUsage
              Storage          → 写入 m_all_hdd_temperature / m_all_hdd_usage
              Motherboard      → GetHardwareTemperature
      m_gpu_temperature        = GpuTemperature()           (Nvidia>Amd>Intel)
      m_main_board_temperature = MainboardTemperature()
      m_cpu_temperature        = (cpu_core_name == AVG)
                                  ? CpuTemperature()
                                  : AllCpuTemperature()[cpu_core_name]
      m_hdd_temperature        = AllHDDTemperature()[hard_disk_name]
      m_hdd_usage              = AllHDDUsage()[hard_disk_name]
      m_cpu_freq / m_gpu_usage = 来自 m_pMonitor（仅在 PDH 未取得时）

[UI 显示]
  DisplayItem::GetItemValueText
    └── CCommon::TemperatureToString / UsageToString / FreqToString
          └── 字段 < 0 → "--"（或 "--°C" / "--%" / "-- GHz"）

[关闭 / 释放]
  if (m_general_data.hardware_monitor_item == 0):
    CSingleLock(&m_minitor_lib_critical)
      m_pMonitor.reset()
        └── ~COpenHardwareMonitor → MonitorGlobal::Instance()->UnInit()
              └── computer->Close()
```

## 异常 / 边界

- Lite 版：`WITHOUT_TEMPERATURE` 定义使 `m_pMonitor` 字段、`InitOpenHardwareMonitorLibThreadFunc` 函数体、`UpdateOpenHardwareMonitorEnableState` 函数体、选项开关硬件监控的代码、`DoMonitorAcquisition` 中的温度采集段、`GetSystemInfoString` 的 `(Lite)` 标记全部不编译；链接器不再需要 `OpenHardwareMonitorApi.lib`。
- `m_pMonitor == nullptr`（`WITHOUT_TEMPERATURE` 或初始化失败）→ 跳过 `GetHardwareInfo`，温度字段保持上次值（启动时 `-1`）。
- `m_pMonitor->GetHardwareInfo` 抛 SEH → `__except` 弹 `IDS_HARDWARE_INFO_ACQUIRE_FAILED_ERROR` 对话框（`TrafficMonitorDlg.cpp:1417-1425`）。
- `m_pMonitor->GetHardwareInfo` 抛托管异常 → `error_message` 写入，下次循环再次弹框（`OpenHardwareMonitorImp.cpp:396-399` + `TrafficMonitorDlg.cpp:1428-1432`）。
- `CreateInstance()` 抛托管异常 → 返回空 `shared_ptr`，主线程弹 `OpenHardwareMonitorApi::GetErrorMessage()`（`OpenHardwareMonitorImp.cpp:37-40` + `TrafficMonitor.cpp:626-629`）。
- `AllCpuTemperature()` 为空 → `m_cpu_temperature = -1`（`TrafficMonitorDlg.cpp:1459-1462`）。
- `AllHDDTemperature()` 为空 → `m_hdd_temperature = -1`（`TrafficMonitorDlg.cpp:1463-1477`）。
- `AllHDDUsage()` 为空且 PDH 未取得 → `m_hdd_usage = -1`（`TrafficMonitorDlg.cpp:1478-1495`）。
- `cpu_core_name` / `hard_disk_name` 在 map 中找不到 → 自动回退到首项并写回 `m_general_data`（`TrafficMonitorDlg.cpp:1450-1457` / `1466-1472` / `1483-1489`）。
- 同名硬盘：`InsertValueToMap` 给 key 加 ` #1` / `#2` 后缀避免 map 冲突（`OpenHardwareMonitorImp.cpp:307-332`）。
- 多显卡优先级：温度 / 利用率都按 `Nvidia → Amd → Intel` 选第一个 ≥ 0 的（`OpenHardwareMonitorImp.cpp:54-62` / `74-82`）。
- PDH 已经取得 `m_cpu_freq` / `m_gpu_usage` 时跳过 OpenHardwareMonitor 的对应读数（`TrafficMonitorDlg.cpp:1437-1440`）。
- `IsTemperatureNeeded()` 仅取决于 `m_general_data.IsHardwareEnable(...)`，与是否实际有硬件无关（`TrafficMonitorDlg.cpp:1029-1030`）。
- `m_minitor_lib_critical` 临界区的作用面**很窄**，仅在两处出现：
  1. `InitOpenHardwareMonitorLibThreadFunc` 内 `m_pMonitor = CreateInstance()` 的赋值（`TrafficMonitor.cpp:624-625`）。
  2. `UpdateOpenHardwareMonitorEnableState` 内 4 个 `SetCpuEnable / SetGpuEnable / SetHddEnable / SetMainboardEnable` 调用（`TrafficMonitor.cpp:1146-1150`）。
  **采集温度/GPU/HDD/CPU 等读取调用（`TrafficMonitorDlg.cpp:1438-1494`）不加锁**——这意味着采集线程与"开关硬件监控使能状态"的设置线程可能撞车，这是当前实现状态，不是建议。