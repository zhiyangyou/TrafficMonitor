# 自动更新与开机自启

> 覆盖主程序自动更新（`CUpdateHelper`）、插件自动更新（`CPluginUpdateHelper`）、开机自启（`auto_start_helper` + `CTrafficMonitorApp::SetAutoRun*`）、崩溃报告（`CRASHREPORT::StartCrashReport` / `crashtool`）、以及单实例互斥（`AppAlreadyRuningDlg` + `CreateMutex`）。不包含网络/磁盘驱动、不包含通知区图标位置调整。

## 1. 主程序更新：`CUpdateHelper`

文件：`TrafficMonitor/UpdateHelper.{h,cpp}`。

### 1.1 更新源：`UpdateSource` 枚举（`UpdateHelper.h:8`）

```cpp
enum class UpdateSource { GitHubSource, GiteeSource };
```

字段映射：`GeneralSettingData::update_source`（`CommonData.h:356`），`int`：0 = GitHub，1 = Gitee。

默认值的差异（`TrafficMonitor.cpp:75`）：

```cpp
bool is_chinese_language{ m_str_table.IsSimplifiedChinese() };
m_general_data.update_source = ini.GetInt(L"general", L"update_source", is_chinese_language ? 1 : 0);
```

简体中文用户默认 Gitee（访问快），其它语言默认 GitHub。

### 1.2 检测流程：`CheckForUpdate()`（`UpdateHelper.cpp:21`）

GitHub 源（`UpdateHelper.cpp:25`）：
1. 先尝试 `raw.githubusercontent.com/zhongyang219/TrafficMonitor/master/version_utf8.info`（row data，`m_row_data = true`）；
2. 失败再试 `github.com/.../blob/master/version_utf8.info`（HTML 页面，`m_row_data = false`），需要把 `&lt;` / `&gt;` 还原为 `<` / `>`（`UpdateHelper.cpp:47`）。

Gitee 源（`UpdateHelper.cpp:54-58`）：只取 `gitee.com/zhongyang219/TrafficMonitor/raw/master/version_utf8.info`。

`ParseUpdateInfo`（`UpdateHelper.cpp:65`）用 `CSimpleXML` 解 version_utf8.info：

| 节点 | 字段 |
|------|------|
| `<version>` | `m_version` |
| `<link>` / `<link_x64>` / `<link_arm64ec>` | x86 / x64 / ARM64EC 下载链接，按当前编译宏 `_M_X64` / `_M_ARM64EC` 取一条 |
| `<update_contents>` 下 `<contents_en>` / `<contents_zh_cn>` / `<contents_zh_tw>` | 三语种更新日志，**`\n` 还原为 `\r\n`** |

`WITHOUT_TEMPERATURE` 编译宏下链接节点改用 `link_without_temperature*`（`UpdateHelper.cpp:73-81`）。

### 1.3 比对与展示：`CTrafficMonitorApp::CheckUpdate`（`TrafficMonitor.cpp:530`）

- 用 `version > VERSION`（当前编译宏里的版本号字符串）判断是否有新版本；
- 根据 `m_str_table.GetLanguageInfo().bcp_47` 选择展示内容（`zh-CN` → `contents_zh_cn`，`zh-TW` → `contents_zh_tw`，其它 → `contents_en`）；
- 用户点 "Yes" 后 `ShellExecute(NULL, _T("open"), link.c_str(), ...)` 跳到下载页（`TrafficMonitor.cpp:592`）；
- 失败/异常分支各有 `IDS_CHECK_UPDATE_FAILD` / `IDS_CHECK_UPDATE_ERROR` 提示。

### 1.4 启动时检查：`check_update_when_start`

`GeneralSettingData::check_update_when_start`（`CommonData.h:355`，默认 true）。

`InitInstance` 末尾（`TrafficMonitor.cpp:1057-1061`）：

```cpp
if (m_general_data.check_update_when_start)
    CheckUpdateInThread(false);   // 不弹提示
```

`CheckUpdateInThread`（`TrafficMonitor.cpp:602`）启一个工作线程跑 `CheckUpdateThreadFunc`（`TrafficMonitor.cpp:607`）：先 `SetThreadLanguage(language_id)`，再调 `CheckUpdate(false)`；当 `m_plugins.GetPlugins()` 非空时**顺便**调 `theApp.m_plugin_update.CheckForUpdate()`（`TrafficMonitor.cpp:614-617`）。

DEBUG 构建下不检查（`TrafficMonitor.cpp:610` `#ifndef _DEBUG`）。

## 2. 插件更新：`CPluginUpdateHelper`

文件：`TrafficMonitor/PluginUpdateHelper.{h,cpp}`。

### 2.1 与 `CUpdateHelper` 的差异

| 维度 | 主程序更新 | 插件更新 |
|------|------------|----------|
| 更新源字段 | `m_general_data.update_source`（int） | 同一个 `m_general_data.update_source == 1` 判断（`PluginUpdateHelper.cpp:88`） |
| 数据格式 | 自定义 XML（`version_utf8.info`） | 标准 `plugins_version.xml` |
| 版本号结构 | 直接字符串比较 `version > VERSION` | `PluginVersion` 类按 `.` 切分整数逐段比较（`PluginUpdateHelper.cpp:7-80`） |
| 比对流程 | 主程序轮询一次，给用户弹"是否下载" | 主程序把每个 `file_name → latest_version` 缓存到 `m_latest_versions`，由插件管理对话框侧按需查询 |
| 入口 | `CheckUpdate`（菜单 / 启动线程） | `CheckForUpdate`（启动线程末尾，或插件管理对话框手动触发） |

### 2.2 关键 API

- `CheckForUpdate()`（`PluginUpdateHelper.cpp:83`）：GET `plugins_version.xml`，遍历根节点下的每个 `<plugin file_name="..." version="..."/>`，构造 `PluginVersion` 存到 `m_latest_versions`；
- `GetPluginLatestVersions(file_name)`（`PluginUpdateHelper.cpp:113`）：按 `file_name`（即 dll 文件名）查最新版本号，返回 `PluginVersion` 常引用；未命中返回静态空对象。

`PluginVersion::operator<`（`PluginUpdateHelper.cpp:45`）按"逐段整数比较，长度不足视作 0"做大小判定——这与主程序直接字符串比较行为不同，要注意不要在主程序里复用此对象做字符串字典序比较。

## 3. 开机自启动：`auto_start_helper` + `CTrafficMonitorApp::SetAutoRun*`

> 重要：仓库提供**两种**开机自启动方式，由用户在选项 → 常规中二选一。
> - **方式 A：注册表 Run 键**（传统方式）
> - **方式 B：Windows 任务计划程序**（替代方式）

`GeneralSettingData`（`CommonData.h:357-358`）：

```cpp
bool auto_run{ false };
bool auto_run_by_task_scheduler{ false };   // true = 任务计划程序；false = 注册表
```

提交 `fd290db "常规设置中增加开机自动运行方式的设置"` 即引入此选项。

### 3.1 顶层入口：`CTrafficMonitorApp::SetAutoRun(auto_run, task_scheduler)`（`TrafficMonitor.cpp:636`）

```cpp
if (task_scheduler)
    return SetAutoRunByTaskScheduler(auto_run);
else
    return SetAutoRunByRegistry(auto_run);
```

互斥逻辑：无论走哪条分支，都会**先禁用另一边**（`TrafficMonitor.cpp:704/734`）—— 防止两种方式并存导致启动两次。

### 3.2 方式 A：注册表 Run 键

打开/写入：`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run` 下的 `APP_NAME`（"TrafficMonitor"）键。

`SetAutoRunByRegistry(bool auto_run)`（`TrafficMonitor.cpp:692`）：
- `auto_run=true` → 先 `SetAutoRunByTaskScheduler(false)` 清掉任务计划；再 `key.SetStringValue(APP_NAME, m_module_path_reg)`；
- `auto_run=false` → 查询键是否存在再删除，失败时弹 `IDS_AUTORUN_DELETE_FAILED`。

`GetAutoRun(path, false)`（`TrafficMonitor.cpp:650-689`）：读 `APP_NAME` 键值，剥掉首尾引号返回；并比较 `m_module_path_reg == buff`（带引号版本）判断是否指向当前 exe。

`m_module_path_reg`（`TrafficMonitor.h:39`）：

> "用于作为写入注册表开机自项的 exe 文件的路径（如果路径中有空格，加上引号）"

构造：`TrafficMonitor.cpp:927-937`：

```cpp
m_module_path = path;            // GetModuleFileNameW(NULL, path, MAX_PATH)
if (m_module_path.find(L' ') != wstring::npos) {
    m_module_path_reg = L'"';
    m_module_path_reg += m_module_path;
    m_module_path_reg += L'"';
}
else {
    m_module_path_reg = m_module_path;
}
```

这是因为 `HKEY_CURRENT_USER\...\Run` 写入的字符串在 Windows 解析时按空格切分命令行，路径含空格必须加引号。

### 3.3 方式 B：Windows 任务计划程序

`auto_start_helper.{h,cpp}` 用 `ITaskScheduler` COM 接口在 `\TrafficMonitor` 目录下注册一个任务。任务名形如 `"Autorun for <USERNAME>"`（`auto_start_helper.cpp:67`），触发器是**登录时**（`TASK_TRIGGER_LOGON`，`auto_start_helper.cpp:155`），延迟 3 秒（`auto_start_helper.cpp:167` 注释："Timing issues may make explorer not be started when the task runs."）。

`create_auto_start_task_for_this_user(bool runElevated)`（`auto_start_helper.cpp:36`）：
- 调 `GetEnvironmentVariable("USERNAME")` 拼 `username_domain = "DOMAIN\USER"`；
- 创建 `\\TrafficMonitor` 文件夹（如不存在）；
- 注册 `TASK_LOGON_INTERACTIVE_TOKEN` 登录方式，`SDDL_FULL_ACCESS_FOR_EVERYONE` DACL；
- `Principal::RunLevel`：`runElevated=true` → `TASK_RUNLEVEL_HIGHEST`（管理员权限），否则 `TASK_RUNLEVEL_LUA`（普通用户）。

`is_auto_start_task_active_for_this_user(path)`（`auto_start_helper.cpp:329`）：
- 通过 `IRegisteredTask::get_Xml()` 拿任务 XML，从中取 `<Exec><Command>` 节点的命令路径，与 `GetModuleFileName(NULL)` 当前路径做**字符串相等**比较（`auto_start_helper.cpp:390` 的 `command_path_match`）；
- 同时读 `Enabled` 属性判断是否启用；
- 返回 `SUCCEEDED(hr) && command_path_match`—— 即**只有"命令路径完全匹配当前 exe 路径"才算"为本程序设置的自启"**。

`delete_auto_start_task_for_this_user()`（`auto_start_helper.cpp:264`）：先 `GetTask`，存在就 `DeleteTask`。

### 3.4 选项 → 常规页的处理

`GeneralSettingsDlg::OnInitDialog` 中（`GeneralSettingsDlg.cpp:286-318`）：

1. 如果启动文件夹存在 `TrafficMonitor.lnk` 旧快捷方式 → 兼容路径：转写为注册表自启；
2. 否则先按"注册表"调用 `GetAutoRun(path, false)`，命中 → 勾选 `IDC_AUTO_RUN_METHOD_REGESTRY_RADIO`；
3. 没命中再按"任务计划"调用 `GetAutoRun(path, true)`，命中 → 勾选 `IDC_AUTO_RUN_METHOD_TASK_SCHEDULE_RADIO`；
4. 都没有 → 不勾选自启动；
5. 若 `WITHOUT_TEMPERATURE` 宏开启，强制 `auto_run_by_task_scheduler=false`（`GeneralSettingsDlg.cpp:280-284`）。

`GeneralSettingsDlg.cpp:215-217` 中"开机自启动"未勾选时，三个相关控件（注册表/任务计划/重置）都禁用。

`OnOK`（`GeneralSettingsDlg.cpp:799`）实际写入：

```cpp
theApp.SetAutoRunByRegistry(false);          // 1. 先两边都关
theApp.SetAutoRunByTaskScheduler(false);
if (!theApp.SetAutoRun(true, m_data.auto_run_by_task_scheduler))   // 2. 按选择重新启用
    ...
```

## 4. 崩溃报告：`CRASHREPORT::StartCrashReport` / `crashtool`

文件：`TrafficMonitor/crashtool.{h,cpp}`。

### 4.1 启动接入

`CRASHREPORT::StartCrashReport()`（`crashtool.cpp:211`）：`::SetUnhandledExceptionFilter(__UnhandledExceptionFilter)` 注册一个全局顶层异常处理器。`__UnhandledExceptionFilter`（`crashtool.cpp:202`）做两件事：

1. `CCrashReport::CreateMiniDump(pEP)` —— 写一个 minidump 文件；
2. `CCrashReport::ShowCrashInfo(pEP)` —— 弹一个 `CMessageDlg` 显示崩溃信息。

返回值 `EXCEPTION_CONTINUE_SEARCH` 表示让默认处理链继续，所以这层不会"吃掉"崩溃——它只是先收集证据。

### 4.2 minidump 写入

`CCrashReport::CreateMiniDump`（`crashtool.cpp:24`）：

- 文件名格式：`<时间>_<模块名>.dmp`，例如 `20150116174802_CrashShare.exe.dmp`（头文件注释 `crashtool.h:5-11`）；
- 路径：`GetTempPath` 的输出（即 `C:\Users\<user>\AppData\Local\Temp\`），失败兜底 `C:\`（`crashtool.cpp:184-191`）；
- `MiniDumpWriteDump` 使用 `MiniDumpNormal` 信息等级；
- 句柄 `hDumpFile` 与文件名都记到 `m_dumpFile`，后续用于提示。

### 4.3 崩溃信息对话框

`ShowCrashInfo`（`crashtool.cpp:143`）：

- 调 `GetStackTrace`（`crashtool.cpp:68`）—— `SymInitialize` + `StackWalk64`（x86 / x64 分支）逐帧解析函数名 + 源文件 + 行号；
- 把栈追加到提示里，再 `theApp.GetSystemInfoString()`（`TrafficMonitor.cpp:741`：Windows 版本等）；
- 把整段写入 `theApp.m_log_path`（一份纯文本崩溃日志）；
- 弹 `CMessageDlg`（`IDS_ERROR_MESSAGE` 标题 + `IDS_CRASH_INFO` 模板 + `SI_ERROR` 图标）。

### 4.4 与 `UpdateHelper` 的关系

`crashtool` 本身**不上传** dump，文件留在 `%TEMP%`。`UpdateHelper` 仅在 `CheckForUpdate` 期间访问 `version_utf8.info`；两者没有直接耦合——开发者侧的"看到崩溃"靠用户主动把 `%TEMP%` 里的 `.dmp` 发回来。

## 5. 单实例：`AppAlreadyRuningDlg` + `CreateMutex`

### 5.1 互斥量

`CTrafficMonitorApp::InitInstance`（`TrafficMonitor.cpp:980-1013`）：

```cpp
m_no_multistart_warning = ini.GetBool(_T("other"), _T("no_multistart_warning"), false);
LPCTSTR mutex_name =
#ifdef _DEBUG
    _T("TrafficMonitor-e8Ahk24HP6JC8hDy");
#else
    _T("TrafficMonitor-1419J3XLKL1w8OZc");
#endif
HANDLE hMutex = ::CreateMutex(NULL, TRUE, mutex_name);
if (hMutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (!m_no_multistart_warning) {
        HWND exist_handel = ::FindWindow(APP_CLASS_NAME, NULL);
        if (exist_handel != NULL) {
            CAppAlreadyRuningDlg dlg(exist_handel);
            dlg.DoModal();
        } else {
            AfxMessageBox(CCommon::LoadText(IDS_AN_INSTANCE_RUNNING));
        }
    }
    return FALSE;          // 退出本进程
}
```

要点：
- 互斥量名包含随机后缀以避免跨用户冲突；
- DEBUG / Release 互斥量不同，互不干扰；
- "已有实例"判定用 **进程内 `CreateMutex` 返回 `ERROR_ALREADY_EXISTS`**，**不是**命名管道；
- 找到 `APP_CLASS_NAME` 主窗口时弹 `CAppAlreadyRuningDlg`（带 4 个按钮：退出实例 / 打开选项 / 显示/隐藏主窗口 / 显示/隐藏任务栏窗口）；找不到则降级为 `AfxMessageBox(IDS_AN_INSTANCE_RUNNING)`；
- 用户设置了 `no_multistart_warning=true`（来自 `[other]` 段）就**静默退出**，连对话框都不弹；
- 即便不弹任何东西，`return FALSE` 仍然阻止本进程启动。

### 5.2 `AppAlreadyRuningDlg` 的能力

文件：`TrafficMonitor/AppAlreadyRuningDlg.{h,cpp}`。构造时接收 `HWND handel`（已存在实例的主窗口句柄），按钮通过 `::PostMessage(m_handle, WM_COMMAND, ID_*, 0)` 把命令**转发给已运行实例**：

| 按钮 | 转发消息 | 效果 |
|------|----------|------|
| `IDC_EXIT_INST_BUTTON` | `ID_APP_EXIT` | 已运行实例退出 |
| `IDC_OPEN_SETTINGS_BUTTON` | `ID_OPTIONS` | 已运行实例打开选项 |
| `IDC_SHOW_HIDE_MAIN_WINDOW_BUTTON` | `ID_SHOW_MAIN_WND` | 主窗口显隐切换 |
| `IDC_SHOW_HIDE_TASKBAR_WINDOW_BUTTON` | `ID_SHOW_TASK_BAR_WND` | 任务栏窗口显隐切换 |

注意：转发的是**窗口消息**，所以已运行实例不需要暴露任何 IPC 接口；这同时也意味着如果已运行实例最小化到托盘、不在前台，对话框弹出后用户点按钮，行为靠已运行实例自身的命令路由完成。

## 6. 关键关系一览

| 关系 | 位置 |
|------|------|
| 更新源默认：中文→Gitee / 其它→GitHub | `TrafficMonitor.cpp:75` |
| 版本号数据源：GitHub raw → 失败回退 HTML | `UpdateHelper.cpp:27-50` |
| `WITHOUT_TEMPERATURE` 宏切换链接节点 | `UpdateHelper.cpp:73-81` |
| 启动时检查更新线程 | `TrafficMonitor.cpp:1057-1061` → `CheckUpdateInThread` → `CheckUpdateThreadFunc` |
| 插件更新与主程序更新共用 `update_source` 字段 | `PluginUpdateHelper.cpp:88` / `UpdateHelper.cpp` |
| 注册表自启键路径 | `TrafficMonitor.cpp:696` |
| `m_module_path_reg` 处理空格 | `TrafficMonitor.cpp:927-937` |
| 任务名格式 `"Autorun for <USER>"` | `auto_start_helper.cpp:67` |
| `is_auto_start_task_active_for_this_user` 严格匹配路径 | `auto_start_helper.cpp:390` |
| 自启方式互斥 | `TrafficMonitor.cpp:704/734` |
| 崩溃 dump 路径 | `GetTempPath`（`crashtool.cpp:185`） |
| 崩溃栈写入 log + 弹对话框 | `crashtool.cpp:143-167` |
| 单实例互斥量名 | `TrafficMonitor.cpp:983-985` |
| 旧实例转发消息 | `AppAlreadyRuningDlg.cpp:58-79` |
| 静默退出开关 | `[other] no_multistart_warning`（`TrafficMonitor.cpp:980`） |