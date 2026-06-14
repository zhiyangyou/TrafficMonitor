# 历史流量统计

> 覆盖 `HistoryTrafficFile`（落盘格式）、`CalendarHelper`（日历视图辅助）、以及 `CHistoryTrafficDlg` 家族（外壳 + 两个 Tab 子页 + 自绘 ListCtrl）。不包括"今日流量"实时显示——后者属于 `DisplayItem` 与 `TrafficMonitor.cpp` 的展示项渲染。

## 1. 数据落盘：`CHistoryTrafficFile`

### 1.1 职责

封装一个文本文件 `history_traffic.dat` 的读写与归一化。
- 内部维护 `deque<HistoryTraffic>`（按日期从大到小排序）；
- 归一化时把"今日"条目作为队首填充；
- 派生字段：`m_today_up_traffic` / `m_today_down_traffic`（单位：字节），从归一化后的首条目 ×1024 得到。

文件位置：`TrafficMonitor/HistoryTrafficFile.{h,cpp}`。

### 1.2 文件格式（`HistoryTrafficFile.cpp:14`）

```
lines: "<条数>"
<YYYY>/<MM>/<DD> <up_kBytes>/<down_kBytes>
<YYYY>/<MM>/<DD> <down_kBytes>           ← 仅一个数 = 旧版/混合数据，mixed=true
...
```

- 第一行 `lines: "N"` 用引号包裹 N，仅供 `LoadSize()` 加速读取（`HistoryTrafficFile.cpp:94`）。
- 数据行有两类：
  - 完整格式：`年/月/日 上行kB/下行kB`（`HistoryTrafficFile.cpp:25`）；
  - 混合格式：`年/月/日 总kB` —— 通过定位第二个 `/` 是否存在来区分，写入时 `mixed==true` 走 24 行分支；读取时若找不到则把整段视为下行、上行清零（`HistoryTrafficFile.cpp:71`）。
- 字段定长：年 4 位、月日各 2 位，偏移见 `HistoryTrafficFile.cpp:57-69` 的 substr。
- 加载上限：`m_history_traffics.size() > 9999` 提前退出（`HistoryTrafficFile.cpp:41`）。

### 1.3 归一化：`MormalizeData()`（`HistoryTrafficFile.cpp:133`）

每次 `Load()` 后被调用，步骤：

1. 空表则先压入"今日 0/0"占位；
2. 按 `HistoryTraffic::DateGreater` 排序（日期从大到小），保证队首为最新日期；
3. 合并相邻同日期条目（上行/下行各自累加）；
4. 关键步骤（`HistoryTrafficFile.cpp:168-177`）：若队首就是"今日"，则把它的 `up_kBytes/down_kBytes × 1024` 写回 `m_today_up_traffic / m_today_down_traffic`，并强制 `mixed=false`；否则在队首插入一个"今日 0/0"的新条目。

`Merge(other, ignore_same_data)`（`HistoryTrafficFile.cpp:112`）提供二合一的合并策略：`ignore_same_data=true` 时用二分查找跳过同日期条目。

### 1.4 文件路径

`TrafficMonitor.cpp:956`：`m_history_traffic_path = m_config_dir + L"history_traffic.dat"`。
`m_history_traffic_path + L".bak"`（`TrafficMonitorDlg.cpp:690`）作为备份。

## 2. `CCalendarHelper`：纯日期数学

`TrafficMonitor/CalendarHelper.{h,cpp}`。
- `IsLeapYear` / `CaculateWeekDay(y,m,d)`（返回 0~6，0=周一，注意：返回 1=周一公式见 `CalendarHelper.cpp:26`）；
- `DaysInMonth` 返回当月天数；
- `GetCalendar(year, month, DayTraffic[6][7], sunday_first)`：填充日历网格，每格 `day=0` 表示"占位但无日期"，仅供 `HistoryTrafficCalendarDlg` 绘制。

`DayTraffic` 结构（`CalendarHelper.h:5`）记录每个日期格的上/下行流量 + `mixed` 标记 + 命中矩形 `rect`（绘制期回填）。

## 3. 对话框家族

外壳：`CHistoryTrafficDlg`（`HistoryTrafficDlg.h`），继承 `CBaseDialog`，内置 `CTabCtrl m_tab` + 两个 `CTabDlg` 子页。

### 3.1 `CHistoryTrafficDlg`（外壳 / `HistoryTrafficDlg.cpp`）

- 构造时把 `deque<HistoryTraffic>&` 引用同时传给两个子页（`HistoryTrafficDlg.cpp:15`）；
- `OnInitDialog` 插入两个 Tab 项："列表视图" / "日历视图"，对应 `IDS_LIST_VIEW` / `IDS_CALENDAR_VIEW`（`HistoryTrafficDlg.cpp:69-70`）；
- `OnTcnSelchangeTab1` 互斥地 `ShowWindow(SW_SHOW/SW_HIDE)` 两个子页；
- `OnSize` 调 `SetTabWndSize` 重排两个子页位置（`HistoryTrafficDlg.cpp:36`）。

入口：
- 主窗口右键菜单命令 `ID_TRAFFIC_HISTORY` → `OnTrafficHistory`（`TrafficMonitorDlg.cpp:2595`）：`CHistoryTrafficDlg historyDlg(m_history_traffic.GetTraffics()); historyDlg.DoModal();`
- 主窗口双击也会触发（`TrafficMonitorDlg.cpp:2631`）。
- 选项设置里无独立"历史流量统计"开关——只要用户进入对话框即显示。

### 3.2 Tab1：`CHistoryTrafficListDlg`（列表视图 / `HistoryTrafficListDlg.cpp`）

- 自绘 `CHistoryTrafficListCtrl m_history_list`，5 列：日期 / 上传 / 下载 / 总流量 / 占比条形图（`HistoryTrafficListDlg.cpp:236-240`）；
- 顶部两个 ComboBox：
  - `m_view_type_combo`：日/周/月/季/年 视图（对应 `HistoryTrafficViewType`，`HistoryTrafficListDlg.cpp:248-253`）；
  - `m_view_scale_combo`：线性/对数刻度（`HistoryTrafficListDlg.cpp:255-257`，影响 `m_use_log_scale`）。
- `ShowListData()`（`HistoryTrafficListDlg.cpp:84`）：
  - **日视图**直接遍历 `m_history_traffics`，每行通过 `CCalendarHelper::CaculateWeekDay` 算出星期再附加 `IDS_SUNDAY..IDS_SATURDAY`；
  - **周/月/季/年视图**通过按 `date_str` 聚合：周视图用 `traffic.week()`（`LoadTextFormat(IDS_WEEK_NUM, ...)`），季视图把月份归到 Q1–Q4；
- `AddListRow` 计算该日总流量，根据量级（<1GB / <10GB / <100GB / <1TB / ≥1TB）分配 `TRAFFIC_COLOR_BLUE/GREEN/YELLOE/RED/DARK_RED`，再调 `m_history_list.SetDrawItemRangeData(index, range, color)` 触发自绘。

### 3.3 Tab2：`CHistoryTrafficCalendarDlg`（日历视图 / `HistoryTrafficCalendarDlg.cpp`）

- 自绘，不使用 ListCtrl：靠 `OnPaint` 在 `m_draw_rect` 内逐格画星期行 + 6×7 日历格；
- `m_year / m_month`：当前查看的月份，年/月 ComboBox 双向切换；`m_year_max / m_year_min` 由 `m_history_traffics.front().year` 与 `back().year` 计算（`HistoryTrafficCalendarDlg.cpp:164-165`）；
- `SetDayTraffic()` 对每个格子做二分查找 `m_history_traffics`（`HistoryTrafficCalendarDlg.cpp:36-50`），命中则回填 `up_traffic / down_traffic / mixed`；未命中保持 0；
- `CalculateMonthTotalTraffic()` 累加 `m_calendar[i][j].up_traffic / down_traffic` 到 `m_month_total_upload / m_month_total_download`，用于底部"本月总流量"信息行（`HistoryTrafficCalendarDlg.cpp:303-307`）；
- 每格右下角画一个 12×12px 的色块，颜色阈值与列表视图一致（`HistoryTrafficCalendarDlg.cpp:277-296`）；
- 今日（`m_year == m_history_traffics[0].year && m_month == m_history_traffics[0].month && day == m_history_traffics[0].day`）画矩形外框（`HistoryTrafficCalendarDlg.cpp:273`）；
- 鼠标悬停通过 `m_tool_tips` 弹气泡：显示日期 + 总流量 + 上/下行（`HistoryTrafficCalendarDlg.cpp:399-411`）；
- 翻页：上一月 / 下一月按钮 + 鼠标滚轮 + 左右方向键（`HistoryTrafficCalendarDlg.cpp:454-499`、`PreTranslateMessage`）；
- 菜单按钮（`IDC_MENU_BUTTON`）弹出子菜单，包含"以周日为首日 / 以周一为首日 / 跳到今天"三项，对应 `m_cfg_data.m_sunday_first`（`HistoryTrafficCalendarDlg.cpp:521-557`）。

### 3.4 `CHistoryTrafficListCtrl`：自绘占比条

`TrafficMonitor/HistoryTrafficListCtrl.{h,cpp}`，继承 `CListCtrl`。
- `EnableDrawItemRange / SetDrawItemRangeRow / SetDrawItemRangeData / SetDrawItemRangInLogScale`：配置自绘列；
- `OnNMCustomdraw`（`HistoryTrafficListCtrl.cpp:34`）拦截 `CDDS_ITEMPREPAINT`，对指定列（默认 4 = 最右列）双缓冲绘制填充矩形：
  - 线性：`width = range * cellWidth / 1000`；
  - 对数：`width = ln(range+1) * cellWidth / ln(1001)`。
  - 即颜色阈值由 `AddListRow` 给出，但矩形宽度由 `range`（归一化到 0~1000）独立控制。

## 4. 数据来源：`m_today_up_traffic` / `m_today_down_traffic` 的落盘闭环

### 4.1 加载

`CTrafficMonitorDlg::LoadHistoryTraffic()`（`TrafficMonitorDlg.cpp:687`）：
1. `m_history_traffic.Load()` —— 触发 `MormalizeData`；
2. 备份文件（`.bak`）若条目更多，则 `Merge(..., true)` 合并并写一行日志 `IDS_HISTORY_TRAFFIC_LOST_ERROR_LOG`（`TrafficMonitorDlg.cpp:690-699`）；
3. 把归一化得到的 `m_today_up_traffic / m_today_down_traffic` 写回 `theApp.m_today_up_traffic / m_today_down_traffic`（`TrafficMonitorDlg.cpp:701-702`）。

### 4.2 运行期累加与跨天切换

`CTrafficMonitorDlg::OnTimer`（或监控刷新点，`TrafficMonitorDlg.cpp:1262-1281`）：
1. `GetLocalTime` 拿到当前年月日；
2. 若 `m_history_traffic.GetTraffics()[0].day != current_time.wDay`，**新建一个 `HistoryTraffic` 压入队首**，并把 `theApp.m_today_up_traffic / m_today_down_traffic` 清零；
3. `theApp.m_today_up_traffic += cur_out_speed; theApp.m_today_down_traffic += cur_in_speed;`（按采样周期累计，**单位是字节**）；
4. 同步回写 `m_history_traffic.GetTraffics()[0].up_kBytes = ... / 1024u`（**注意换算**：显示项拿 `m_today_*_traffic` 是字节，但落盘是 kBytes，差 1024 倍）。

### 4.3 节流落盘

仍在同一函数（`TrafficMonitorDlg.cpp:1283-1289`）：仅当 `m_monitor_time_cnt % (30s/timer) == (30s/timer)-1` **且**当日累计比上次落盘多 ≥100KB 时，才调 `SaveHistoryTraffic()` —— 防止 IO 抖动。`SaveHistoryTraffic()`（`TrafficMonitorDlg.cpp:682`）就是 `m_history_traffic.Save()`。

另外两个落盘点：`WM_CLOSE` 时（`TrafficMonitorDlg.cpp:2137`）、任务栏图标右键菜单退出时（`TrafficMonitorDlg.cpp:2756`）。

## 5. 配置联动

`MainConfigData`（`CommonData.h:216-218`）中与历史流量相关的字段：

| 字段 | 含义 | 读取点 |
|------|------|--------|
| `m_use_log_scale` | 列表视图占比条是否用对数刻度 | `HistoryTrafficListCtrl.cpp:23` |
| `m_view_type` | `HistoryTrafficViewType`：日/周/月/季/年 | `HistoryTrafficListDlg.cpp:89/151/156/160` |
| `m_sunday_first` | 日历视图是否以周日为首日 | `HistoryTrafficCalendarDlg.cpp:55/99/107/537/545` |

## 6. 入口与触发面汇总

| 入口 | 位置 |
|------|------|
| 主窗口右键菜单 "历史流量统计" | `ID_TRAFFIC_HISTORY` → `TrafficMonitorDlg.cpp:2595` |
| 主窗口双击 | `TrafficMonitorDlg.cpp:2631` |
| 任务栏窗口右键菜单 | `ID_OPTIONS2`（待确认：`TaskBarDlg.cpp:1131` 把 `ID_OPTIONS2` 设为默认项；该菜单项同样进 OnOptions→指定 tab） |
| 选项设置中的"历史流量"开关 | **无独立开关**；只在"常规设置"中可调 `m_view_type / m_use_log_scale / m_sunday_first` |