# 数据采集层

本文件描述 TrafficMonitor 主程序中所有"采集一次完整监控数据"的代码路径：网速、CPU/内存/显卡/硬盘利用率、网卡选择、累计流量，以及它们的线程模型与缓存形态。

## 职责

把 Windows 内核数据（IPHLPAPI、PDH、GetSystemTimes、GlobalMemoryStatusEx）按一个固定周期（`monitor_time_span`）采样后写入 `CTrafficMonitorApp` 上的公共成员变量，再由 UI 线程（主对话框、任务栏窗口、托盘菜单、插件回调）按需读取。

## 关键类 / 函数

### 1. 网速采集链路

#### Windows API 入口
- `GetIfTable(MIB_IFTABLE*, DWORD*, BOOL)`：IPHLPAPI 提供的主表，存放每张网卡的累计 `dwInOctets` / `dwOutOctets` / `bDescr` / `dwOperStatus`。
- `GetAdaptersInfo(PIP_ADAPTER_INFO, DWORD*)`：IPHLPAPI 提供的另一份表，按 GetAdaptersInfo 描述符枚举，用于拿到 IP、子网掩码、默认网关。
- `GetNumberOfInterfaces(DWORD*)`：IPHLPAPI，仅用于周期性探测网卡数量是否发生变化。

调用点统一在 `CTrafficMonitorDlg::DoMonitorAcquisition()` 内的 lambda `getLfTable()` 中，`TrafficMonitorDlg.cpp:1168-1186`。这段代码用 `__try / __except` 捕获 `GetIfTable` 的 SEH 异常，若崩溃则重新 `malloc` 并重试。

#### 网卡描述符 / IP 桥接类
- `CAdapterCommon`（`TrafficMonitor/AdapterCommon.h:17`，实现 `AdapterCommon.cpp:1-163`）：三个静态方法：
  - `GetAdapterInfo(vector<NetWorkConection>&)` → 用 `GetAdaptersInfo` 填充 IP/掩码/网关。
  - `GetIfTableInfo(vector<NetWorkConection>&, MIB_IFTABLE*)` → 用 IPHLPAPI 描述符在 `MIB_IFTABLE` 中精确匹配，再退到模糊匹配 (`FindConnectionInIfTableFuzzy`)，最后退到 Levenshtein 相似度匹配 (`StringSimilarDegree_LD`)。
  - `GetAllIfTableInfo(vector<NetWorkConection>&, MIB_IFTABLE*)` → 当 `show_all_interface=true` 时枚举 `MIB_IFTABLE` 中所有条目，再用模糊匹配把 IP 信息贴回去。

#### 数据结构
- `struct NetWorkConection`（`TrafficMonitor/AdapterCommon.h:5-15`）：一张网卡的视图，字段包括在 `MIB_IFTABLE` 中的 `index`、两份描述符（`description` 来自 `GetAdaptersInfo`，`description_2` 来自 `MIB_IFTABLE.bDescr`）、累计接收/发送字节数、IP/掩码/网关。

#### 网速计算
- `CTrafficMonitorDlg::DoMonitorAcquisition()`（`TrafficMonitorDlg.cpp:1164-1281`）一次采集的主流程：
  - 通过 `GetConnectIfTable(m_connection_selected)` 或遍历 `m_connections` 拿到本次累计字节数；
  - 与上次累计值（`m_last_in_bytes` / `m_last_out_bytes`）作差得到 `cur_in_speed` / `cur_out_speed`（字节数）；
  - 异常边界：`m_in_bytes == 0 && m_out_bytes == 0`、`m_last_in_bytes == 0 && m_last_out_bytes == 0`、`m_connection_change_flag` 为真、或累计值倒着减小（连接刚换 / 计数器回绕），这四种情况下 `cur_in_speed` 与 `cur_out_speed` 都被强制归零（`TrafficMonitorDlg.cpp:1214-1224`）；
  - 用 `CCommon::GetCurrentTimeSinceEpochMilliseconds()` 算两次采样的真实时间差 `time_span`（默认等于 `m_general_data.monitor_time_span`，但实际值是两次 `OnTimer` 的真实时间差，更准确），按 `cur_in_speed * 1000 / time_span` 转换为每秒字节数；
  - 写入全局缓存：`theApp.m_in_speed` / `theApp.m_out_speed`（`TrafficMonitorDlg.cpp:1240-1241`）。

### 2. CPU / 内存 / GPU / HDD / CPU 频率采集链路

所有非网络指标都通过 `PdhHardwareQuery/` 目录下的 PDH 包装类完成，共用一个基类 `CPdhQuery`：

#### 基类 `CPdhQuery`（`TrafficMonitor/PdhHardwareQuery/PdhQuery.h:5`，实现 `PdhQuery.cpp:1-114`）
- 构造时接收一个 PDH 计数器路径，依次调用 `PdhOpenQuery` → `PdhAddCounter`（失败时退回 `PdhAddEnglishCounter`） → `PdhCollectQueryData` 初始化；
- `QueryValue(double&)`：每次采集调用 `PdhCollectQueryData` + `PdhGetFormattedCounterValue`，取 `PDH_FMT_DOUBLE`；
- `QueryValues(vector<CounterValueItem>&)`：使用 `PdhGetFormattedCounterArray` 取多实例（如 `PhysicalDisk(*)`、`GPU Engine(*)`），用 `malloc` 二次申请处理 `PDH_MORE_DATA`；
- 析构调用 `PdhCloseQuery`。

#### CPU 利用率
- `CPdhCPUUsage`（`TrafficMonitor/PdhHardwareQuery/CPUUsage.h:6`，实现 `CPUUsage.cpp:11-27`）：构造时根据 `theApp.m_win_version.GetMajorVersion() >= 10` 选择 `\\Processor Information(_Total)\\% Processor Utility`（Win10+）或 `\\Processor Information(_Total)\\% Processor Time`（旧版本）。
- `CCPUUsage`（`TrafficMonitor/PdhHardwareQuery/CPUUsage.h:20`，实现 `CPUUsage.cpp:31-80`）：对外门面。
  - `GetCpuUsage()`：先调 `m_pdh_cup_usage_query.GetCPUUsage(...)`，失败时 fallback 到 `GetCpuUsageByGetSystemTimes()`（实现用 `GetSystemTimes` + 前后两次 `FILETIME` 差，按 `(kernel+user-idle)/(kernel+user)` 计算）。
  - 调用点：`TrafficMonitorDlg.cpp:1354` 的 `theApp.m_cpu_usage = m_cpu_usage_helper.GetCpuUsage();`。
  - 历史：曾经的"CPU 使用率获取方式"选项（pdh / CPU 时间 / 自动）已被移除（见 commit `b6e1247`），当前默认 PDH，失败自动 fallback。

#### CPU 频率
- `CPdhCpuFreq`（`TrafficMonitor/PdhHardwareQuery/CpuFreq.h:6`，实现 `CpuFreq.cpp:19-43`）：计数器路径 `\\Processor Information(_Total)\\% Processor Performance`；构造时调用 `CallNtPowerInformation(ProcessorInformation, ...)` 拿到每核最大频率 `MaxMhz`，再用 `value / 100 * max_cpu_freq` 算出当前频率（GHz）。
- 调用点：`TrafficMonitorDlg.cpp:1359`，写入 `theApp.m_cpu_freq`。代码注释明确"第二次调用开始才能获取到值，两次调用的时间不应过短，最好大于 200 毫秒"。

#### 内存利用率
- 不走 PDH，直接调用 Win32 `GlobalMemoryStatusEx`（`TrafficMonitorDlg.cpp:1402-1407`）：写入 `theApp.m_memory_usage`（`statex.dwMemoryLoad`，即占用百分比）、`theApp.m_used_memory`（已用 KB）、`theApp.m_total_memory`（总 KB）。

#### 显卡利用率
- `CPdhGPUUsage`（`TrafficMonitor/PdhHardwareQuery/GpuUsage.h:5`，实现 `GpuUsage.cpp:7-51`）：计数器路径 `\\GPU Engine(*)\\Utilization Percentage`。
- `GetGpuUsage(int&)`：把所有实例按 `_` 切出后缀（3D / Copy / Video 等引擎），按后缀累加到 `std::map`，最后取所有引擎中最大的累计值（与 Windows 任务管理器一致）。
- 调用点：`TrafficMonitorDlg.cpp:1366`（仅在 Lite 版 / GPU 未启用硬件监控时使用），写入 `theApp.m_gpu_usage`。

#### 硬盘利用率
- `CPdhDiskUsage`（`TrafficMonitor/PdhHardwareQuery/DiskUsage.h:4`，实现 `DiskUsage.cpp:6-84`）：计数器路径 `\\PhysicalDisk(*)\\% Idle Time`。
- 构造时预热一次以拿到所有物理磁盘实例名（`m_diskNames`），`GetDiskUsage(int diskIndex, int& usage)` 按下标取值。
- `CalculateUtilization(double idleTime)` 处理 NVMe / RAID 多队列的边界——空闲时间可能大于 100%，代码里 `if (idleTime > 100.0) idleTime = 100.0;`（`DiskUsage.cpp:42-43`）。
- `FindDiskIndex(const std::wstring diskName)`：按用户在 `m_general_data.hard_disk_name` 里写的名字反查下标；找不到时退化到 `_Total`，再找不到用 `m_diskNames.front()`，三者全部失败则 `theApp.m_hdd_usage = -1`（`TrafficMonitorDlg.cpp:1375-1398`）。
- 调用点：`TrafficMonitorDlg.cpp:1395` 写入 `theApp.m_hdd_usage`，并置 `m_get_disk_usage_by_pdh = true` 标记。

### 3. 网卡选择 / 过滤逻辑

入口在 `CTrafficMonitorDlg::IniConnection()`（`TrafficMonitorDlg.cpp:385-485`），由若干 UI 事件触发（启动 `OnInitDialog:1076`、选项变更 `793`、`ERROR_INSUFFICIENT_BUFFER` 错误 `1295`、网卡数变化 `1317`、`connections.log` 不匹配 `1337`、插件初始化失败重试 `3033`）。

#### 关键配置项（`TrafficMonitor/CommonData.h:380-401`）
- `show_all_interface{ true }`：`m_general_data.show_all_interface`，"显示所有网络连接"开关。
- `connections_hide`：`StringSet`，用户手动指定要从列表里隐藏的 GetAdaptersInfo 描述符集合。
- `m_select_all{ false }`：`m_cfg_data.m_select_all`，"选择全部"——按所有网卡总流量求和显示。
- `m_auto_select`：`m_cfg_data.m_auto_select`，自动按"已收+已发字节数最多"且 `dwOperStatus == IF_OPER_STATUS_OPERATIONAL` 的连接作为当前连接（`AutoSelect`，`TrafficMonitorDlg.cpp:361-383`）。
- `m_connection_name`：`m_cfg_data.m_connection_name`，保存用户上次选定的 `MIB_IFTABLE.bDescr`，重启后用来还原选择（`TrafficMonitorDlg.cpp:475`）。

#### 过滤流程（`TrafficMonitorDlg.cpp:401-416`）
```
if (!show_all_interface) {
    GetAdapterInfo(connections);                  // 仅拿有 IP 的接口
    for each conn: if !connections_hide.contains(conn.description) m_connections.push_back(conn);
    GetIfTableInfo(m_connections, m_pIfTable);    // 用 IPHLPAPI 描述符去 IfTable 里取 index + 累计字节
} else {
    GetAllIfTableInfo(m_connections, m_pIfTable); // 枚举 IfTable 全部条目
}
if (show_all_interface && m_select_all) {        // 互斥
    m_select_all = false;
    m_auto_select = true;
}
```

随后根据 `m_auto_select` 决定是 `AutoSelect()`（`TrafficMonitorDlg.cpp:361-383`，选 `dwInOctets + dwOutOctets` 最大的 `IF_OPER_STATUS_OPERATIONAL` 接口）还是用 `m_connection_name_preferd` 找回上次的索引。

`m_zero_speed_cnt` 计数器（`TrafficMonitorDlg.cpp:1249-1260`）在 `m_auto_select` 模式下生效：连续 30 秒零流量触发一次 `AutoSelect()`。

#### 异常边界
- `ERROR_INSUFFICIENT_BUFFER`：`GetIfTable` 返回此值时重新申请 `m_dwSize` 大小的缓冲区（`TrafficMonitorDlg.cpp:393-397`）。
- SEH 异常：`DoMonitorAcquisition` 内 `__try / __except (EXCEPTION_EXECUTE_HANDLER)` 重新 malloc 并重试（`TrafficMonitorDlg.cpp:1169-1186`）。
- 网卡数变化：每 `GetMonitorTimerCount(3)` 次（约 3 秒）调用 `GetNumberOfInterfaces`，数量变化时 `IniConnection()`（`TrafficMonitorDlg.cpp:1301-1319`）。
- 描述符不匹配：`m_connection_selected` 对应的 `bDescr` 与 `m_connection_name` 不一致时也 `IniConnection()`（`TrafficMonitorDlg.cpp:1321-1341`）。

### 4. 今日流量累计

`m_today_up_traffic` / `m_today_down_traffic` 是 `CTrafficMonitorApp` 上的 `unsigned __int64` 公共成员（`TrafficMonitor/TrafficMonitor.h:62-63`）。

#### 增量公式
在 `DoMonitorAcquisition()` 的网速段尾部（`TrafficMonitorDlg.cpp:1262-1281`）：
```
if (m_history_traffic.GetTraffics()[0].day != current_time.wDay) {  // 日期翻页
    push_front(new HistoryTraffic{...});
    m_today_up_traffic = 0;
    m_today_down_traffic = 0;
}
m_today_up_traffic   += cur_out_speed;   // 注意：是字节差，不是每秒速率
m_today_down_traffic += cur_in_speed;
m_history_traffic.GetTraffics()[0].up_kBytes   = m_today_up_traffic   / 1024u;
m_history_traffic.GetTraffics()[0].down_kBytes = m_today_down_traffic / 1024u;
```
注意：累加用的是 `cur_in_speed / cur_out_speed`（即一个采样周期内的字节差），单位是字节。

#### 持久化
`CHistoryTrafficFile`（`TrafficMonitor/HistoryTrafficFile.h:3`）持有 `deque<HistoryTraffic> m_history_traffics` 与 `m_today_up_traffic` / `m_today_down_traffic`，提供 `Save()` / `Load()` / `Merge()`。
- 启动时：`LoadHistoryTraffic()`（`TrafficMonitorDlg.cpp:1110`）→ `theApp.m_today_up_traffic = m_history_traffic.GetTodayUpTraffic()`（`TrafficMonitorDlg.cpp:701-702`）。
- 每 30 秒且变化 ≥ 100KB 时：`SaveHistoryTraffic()`（`TrafficMonitorDlg.cpp:1283-1290`）。

### 5. 数据缓存：CTrafficMonitorApp 上的"全局可读"模型

`CTrafficMonitorApp`（`TrafficMonitor/TrafficMonitor.h:31-216`）上的公共成员变量就是采集层和 UI 层之间的共享缓存：

| 字段 | 类型 | 默认值 | 来源 |
|---|---|---|---|
| `m_in_speed` / `m_out_speed` | `unsigned __int64` | 0 | `DoMonitorAcquisition` 网速段 |
| `m_cpu_usage` | `int` | -1 | `CCPUUsage::GetCpuUsage()` |
| `m_memory_usage` | `int` | -1 | `GlobalMemoryStatusEx` |
| `m_used_memory` / `m_total_memory` | `int` (KB) | 0 | 同上 |
| `m_cpu_temperature` | `float` | -1 | `m_pMonitor->CpuTemperature` / `AllCpuTemperature` |
| `m_cpu_freq` | `float` | -1 | `m_pMonitor->CpuFreq` 或 `CPdhCpuFreq` |
| `m_gpu_temperature` | `float` | -1 | `m_pMonitor->GpuTemperature` |
| `m_hdd_temperature` | `float` | -1 | `m_pMonitor->AllHDDTemperature` |
| `m_main_board_temperature` | `float` | -1 | `m_pMonitor->MainboardTemperature` |
| `m_gpu_usage` | `int` | -1 | `CPdhGPUUsage` 或 `m_pMonitor->GpuUsage` |
| `m_hdd_usage` | `int` | -1 | `CPdhDiskUsage` 或 `m_pMonitor->AllHDDUsage` |
| `m_today_up_traffic` / `m_today_down_traffic` | `unsigned __int64` | 0 | 历史文件 + 增量累加 |

注释 `//以下数据定义为App类中的公共成员，以便于在主对话框和任务栏窗口中都能访问`（`TrafficMonitor.h:47`）明确说明这套"采集完→全局可读"的简化模型——任何线程都可以读，但写入方只有一个：`CTrafficMonitorDlg::DoMonitorAcquisition()`。

### 6. 主程序怎么驱动采集（OnTimer / WM_TIMER）

#### 两个 WM_TIMER 入口
- `MAIN_TIMER`（`TrafficMonitor/stdafx.h:83`，值 `1234`）：1000 ms，仅做 UI 1 秒节奏上的事情（启动后 1 秒的特殊处理、`m_timer_cnt` 自增等），不直接采集数据。
- `MONITOR_TIMER`（`TrafficMonitor/stdafx.h:87`，值 `1238`）：周期 = `theApp.m_general_data.monitor_time_span`（`CommonData.h:382`，默认 1000 ms，有效范围 200–30000 ms，由 `MONITOR_TIME_SPAN_MIN/MAX` 定义在 `CommonData.h:404-405`）。

启动时序（`TrafficMonitorDlg.cpp:1113-1116`）：
```
SetTimer(MAIN_TIMER, 1000, NULL);
SetTimer(MONITOR_TIMER, monitor_time_span, NULL);
AfxBeginThread(MonitorThreadCallback, this);
```

#### MONITOR_TIMER → 工作线程
`OnTimer` 收到 `MONITOR_TIMER` 时（`TrafficMonitorDlg.cpp:1570-1574`）只做一件事：把 `m_monitor_data_required = true`。

真正干活的是 `MonitorThreadCallback`（`TrafficMonitorDlg.cpp:1527-1554`）：循环里 `if (m_monitor_data_required) { DoMonitorAcquisition(); m_monitor_data_required = false; } else Sleep(10);`。这意味着采集发生在后台工作线程中。

#### 数据回流到 UI
`DoMonitorAcquisition` 末尾（`TrafficMonitorDlg.cpp:1524`）调用 `SendMessage(WM_MONITOR_INFO_UPDATED)`（`WM_USER+1007`，`stdafx.h:71`），由主线程的 `OnMonitorInfoUpdated`（`TrafficMonitorDlg.cpp:2832-2847`）处理：`Invalidate(FALSE)` 触发主窗口重绘，并刷新两个 tooltip。

#### 监测周期内的二级节拍
- 每 30 秒：`SaveHistoryTraffic()`（`TrafficMonitorDlg.cpp:1283-1290`）。
- 每 3 秒：检查 `GetNumberOfInterfaces` 是否变化、检查描述符是否匹配（`TrafficMonitorDlg.cpp:1301-1341`）。
- 30 秒零流量 + `m_auto_select`：`AutoSelect()`（`TrafficMonitorDlg.cpp:1249-1260`）。

### 7. 多线程 / 线程安全

#### 采集线程模型
- 工作线程：`MonitorThreadCallback`，写入 `theApp.m_*` 字段。
- 主 UI 线程：`OnMonitorInfoUpdated` 与 `GetMonitorValue` / `GetMonitorValueString`（`TrafficMonitor.cpp:1400-1452`，提供给插件 API）从 `theApp.m_*` 读。

#### `m_minitor_lib_critical` 临界区
定义在 `TrafficMonitor/TrafficMonitor.h:106`：`CCriticalSection m_minitor_lib_critical;  //用于访问OpenHardwareMonitor进行线程同步的临界区对象`。

它的作用域严格限定在 `m_pMonitor`（OpenHardwareMonitor 包装对象）——所有读 / 写都用 `CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);` 包起来：
- `CTrafficMonitorApp::InitOpenHardwareMonitorLibThreadFunc`：`TrafficMonitor.cpp:624`，持有锁创建 `m_pMonitor`。
- `CTrafficMonitorApp::UpdateOpenHardwareMonitorEnableState`：`TrafficMonitor.cpp:1146`，调用 `SetCpuEnable / SetGpuEnable / SetHddEnable / SetMainboardEnable`。
- `CTrafficMonitorDlg::DoMonitorAcquisition`：`TrafficMonitorDlg.cpp:1413`，调用 `m_pMonitor->GetHardwareInfo()` 并读温度 / 频率 / 利用率。
- 选项设置对话框关闭硬件监控时：`TrafficMonitorDlg.cpp:808-809` 的 `m_pMonitor.reset()` 也在锁内执行。

注释明确：`//CCriticalSection m_lftable_critical;  //用于访问LfTable2进行线程同步的临界区对象`（`TrafficMonitor.h:107`）——这条被注释掉，意味着 IPHLPAPI 调用不做专门的同步保护（IPHLPAPI 自身的实现被认为是线程安全的，`MIB_IFTABLE` 也在工作线程自己的栈上 malloc）。

#### 其他线程标志
- `m_is_thread_exit` / `m_threadExitEvent`：通知工作线程退出并等待（`TrafficMonitorDlg.cpp:1545-1563`）。
- `m_monitor_data_required`：bool 标志位，由 `OnTimer` 置 true，由 `MonitorThreadCallback` 消费后置 false。

## 调用链

```
OnInitDialog (TrafficMonitorDlg.cpp:1035)
  ├── LoadHistoryTraffic()                                   // m_today_*_traffic 初值
  ├── SetTimer(MAIN_TIMER, 1000)                             // 1 秒 UI 节拍
  ├── SetTimer(MONITOR_TIMER, monitor_time_span)             // 数据采集节拍
  ├── AfxBeginThread(MonitorThreadCallback, this)            // 启动工作线程
  └── IniConnection()                                        // 初始化网卡列表 + MIB_IFTABLE

[工作线程 MonitorThreadCallback]
  while (!m_is_thread_exit) {
    if (m_monitor_data_required) {
      DoMonitorAcquisition() ----------------------------------+-- m_pIfTable = GetIfTable()
        ├── getLfTable()                                       |   m_connections = IniConnection()
        ├── 累计字节差 → cur_in_speed / cur_out_speed          |
        ├── theApp.m_in_speed / m_out_speed                    |
        ├── 日期翻页? → 重置 m_today_*_traffic               |
        ├── m_today_*_traffic += cur_*_speed                  |
        ├── 每 30 秒 SaveHistoryTraffic()                    |
        ├── 每 3 秒 检查网卡数 / 描述符                       |
        ├── m_cpu_usage = m_cpu_usage_helper.GetCpuUsage()    |-- CCPUUsage
        ├── m_cpu_freq  = m_cpu_freq_helper.GetCpuFreq()      |-- CPdhCpuFreq
        ├── m_gpu_usage = m_gpu_usage_helper.GetGpuUsage()    |-- CPdhGPUUsage  (Lite 或 GPU 未启用硬件监控)
        ├── m_hdd_usage = m_disk_usage_helper.GetDiskUsage()  |-- CPdhDiskUsage (Lite 或 HDD 未启用硬件监控)
        ├── m_memory_usage = GlobalMemoryStatusEx             |
        ├── IsTemperatureNeeded && m_pMonitor != nullptr:    ----+
        │     CSingleLock(&m_minitor_lib_critical)            |  OpenHardwareMonitor
        │     m_pMonitor->GetHardwareInfo()                   |  COpenHardwareMonitor
        │     m_pMonitor->CpuTemperature()                    |
        │     m_pMonitor->AllCpuTemperature()  ← cpu_core_name|
        │     m_pMonitor->GpuTemperature()                    |
        │     m_pMonitor->AllHDDTemperature()  ← hard_disk_name
        │     m_pMonitor->AllHDDUsage()        ← hard_disk_name
        │     m_pMonitor->CpuFreq() / GpuUsage()              |
        ├── 插件回调 plugin->OnMonitorInfo(monitor_info)      |
      SendMessage(WM_MONITOR_INFO_UPDATED)                    --+
    } else Sleep(10);
  }

[主线程 OnMonitorInfoUpdated]
  Invalidate(FALSE) + UpdateToolTips()
  └── UI 读 theApp.m_*（经 DisplayItem::GetItemValueText / GetMonitorValue）
```

## 异常 / 边界

- 网速零值或计数器回绕：差值为 0、计数器反转、`m_connection_change_flag` 为真 → 强制 `cur_*_speed = 0`（`TrafficMonitorDlg.cpp:1214-1224`）。
- `GetIfTable` 抛 SEH 异常 → `__except` 中重新 malloc 后重试（`TrafficMonitorDlg.cpp:1173-1186`）。
- `GetIfTable` 返回 `ERROR_INSUFFICIENT_BUFFER` → 重新按 `m_dwSize` 申请 + 写日志（`TrafficMonitorDlg.cpp:393-397` 与 `1293-1299`）。
- 网卡数变化（`GetNumberOfInterfaces` 每 3 秒检查）→ `IniConnection()`（`TrafficMonitorDlg.cpp:1301-1319`）。
- 描述符不匹配（`bDescr != m_connection_name`）→ `IniConnection()`（`TrafficMonitorDlg.cpp:1321-1341`）。
- `m_auto_select` 下 30 秒零流量 → 自动重选（`TrafficMonitorDlg.cpp:1249-1260`）。
- PDH CPU 失败 → fallback `GetSystemTimes`（`CPUUsage.cpp:39-50`）。
- PDH GPU / HDD 失败 → 字段写 `-1`，由 UI `UsageToString(<0)` 渲染成 `--`（`Common.cpp:362-376`）。
- PDH CPU 频率首次 / 间隔 < 200 ms → PDH 未预热，`GetCpuFreq` 返回 `false`，UI 用 `-1` 哨兵显示 `--`（`CpuFreq.cpp` 注释 + `TrafficMonitorDlg.cpp:1359`）。
- `m_pMonitor == nullptr`（`WITHOUT_TEMPERATURE` 或初始化失败）→ 跳过整段硬件监控，温度字段保持 `-1` 哨兵（`TrafficMonitorDlg.cpp:1411`）。
- `m_pMonitor->GetHardwareInfo` 抛托管异常 → `__except` 捕获后弹 `IDS_HARDWARE_INFO_ACQUIRE_FAILED_ERROR` 对话框（`TrafficMonitorDlg.cpp:1417-1425`）；非托管异常被 `error_message` 捕获后再弹一次（`TrafficMonitorDlg.cpp:1428-1432`）。
- `m_pMonitor->AllCpuTemperature` 为空 → `m_cpu_temperature = -1`（`TrafficMonitorDlg.cpp:1459-1462`）。
- `m_pMonitor->AllHDDTemperature` 为空 → `m_hdd_temperature = -1`（`TrafficMonitorDlg.cpp:1463-1477`）。
- `m_pMonitor->AllHDDUsage` 为空且 PDH 未取得 → `m_hdd_usage = -1`（`TrafficMonitorDlg.cpp:1478-1495`）。
- `cpu_core_name` / `hard_disk_name` 在 `All*` map 中找不到 → 自动回退到首项并写回 `m_general_data`（`TrafficMonitorDlg.cpp:1450-1457` / `1466-1472` / `1483-1489`）。
- `monitor_time_span` 改值后立即 `KillTimer` + `SetTimer`（`TrafficMonitorDlg.cpp:798-799`）。
- 历史流量变化 ≥ 100 KB 才落盘，避免磁盘抖动（`TrafficMonitorDlg.cpp:1286-1290`）。
- `monitor_time_span` 范围硬约束 `MONITOR_TIME_SPAN_MIN=200` / `MAX=30000`（`CommonData.h:404-405`）。