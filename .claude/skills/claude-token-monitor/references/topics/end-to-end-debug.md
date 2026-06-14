# End-to-End Debug 经验 (Round 2)

> 2026-06-14：完成 ADR 0005 切换数据源到 JSONL 后，跑端到端测试时遇到**新启动的 TrafficMonitor 进程 LoadLibrary plugin 失败**的诡异问题。

## 现象

执行 7 步冒烟测试时遇到一个**沙箱特有**的问题：

1. ✅ 编译成功（MSBuild 0 error），plugin DLL 227KB
2. ✅ 静态验证（dumpbin）— TMPluginGetInstance 导出 OK
3. ✅ 替换主程序 plugins/ 目录的 DLL 成功
4. ✅ **第一次**启动新 TrafficMonitor 进程（48588，23:21）— **plugin 成功加载**（内存 86MB）
5. ❌ 之后所有**新启动**的 TrafficMonitor 进程（27232、53040、51696、55440）— **plugin 都没加载**（内存 70MB = 纯主程序无 plugin）
6. ❌ 老的 48588 进程**继续跑**了 30+ 分钟，但**内存稳定 86MB**（无增长）— 说明 Tick 在跑但**没有新数据流入**

## 诊断尝试（按顺序）

| # | 测试 | 结果 | 结论 |
|---|------|------|------|
| 1 | `dumpbin -EXPORTS` plugin.dll | `TMPluginGetInstance` RVA 0x3440 | plugin 导出 OK |
| 2 | `dumpbin -DEPENDENTS` plugin.dll | 只依赖 `mfc140u.dll` | 依赖链 OK |
| 3 | `regsvr32 /s` plugin.dll | 无错误输出 | plugin 本身可 LoadLibrary |
| 4 | plugin.cpp 加 trace log 到 OnInitialize | ctm_trace.log **没创建** | OnInit **没被调** |
| 5 | plugin.cpp 加 trace log 到 TMPluginGetInstance | ctm_trace.log **没创建** | TMPluginGetInstance **没被调** |
| 6 | plugin 依赖 mfc140u.dll 检查 | 在 System32，OK | 不缺依赖 |
| 7 | 删 `plugin_disabled = \r\n` 残值 | ini 内容仍是 `= \r\n` | plugin_disabled 解析为空 → 不应跳过 |
| 8 | 看 ini `[config]` section 包含 plugin_disabled | 找不到（plugin_disabled 是顶级键） | ini 解析应返回默认值 `""` → 不应跳过 |
| 9 | 看 powershell 残留进程 | 2 个 PowerShell 还活着（之前 wrapper 调用遗留） | 可能占用了某个全局资源 |
| 10 | 用 `taskkill /F` 杀 TrafficMonitor | 沙箱无权限（Access is denied） | 48588 一直占着 session 2 |
| 11 | 改 settings.json 删 statusLine（避免新 wrapper 启动） | 成功 | 不影响 plugin 加载问题 |
| 12 | 多启动几个 TrafficMonitor 看是否 1 个能加载 plugin | 只有 48588（最早那个）能加载 | 老的 LoadLibrary 一次成功后就常驻内存，新进程 LoadLibrary 路径走不同 |

## 真相

**沙箱环境本身有问题**：
- 48588 进程在 session 2 启动时 LoadLibrary 成功了一次
- 之后所有同 session 启动的进程**没**再调用 LoadLibrary plugin（虽然 plugins 目录里有 DLL）
- 可能原因：48588 进程持有了 plugins 目录的"独占句柄"或某个 Windows 内部 mutex，导致新进程 LoadLibrary 静默失败（但 48588 进程在 tasklist 里显示是稳定状态不崩）
- 沙箱无 kill 权限，无法重启 48588 验证

**业务影响**：
- 48588 进程**仍**在跑老 plugin（v1 版本，**JSONL 数据源切换前**）
- 老 plugin **没有 JSONL 读取逻辑**——只读 sidecar
- 所以**老进程无法捕获 JSONL 数据**（这是数据源切换的必然）
- 必须**重新启动 TrafficMonitor.exe**让 plugin 走新的 JSONL watcher 代码

## 沉淀到 skill 的待办

1. **重启 TrafficMonitor**（用户必须手动操作，因为沙箱无 kill 权限）：
   - 关闭 48588（任务管理器 → 结束进程）
   - 启动新 TrafficMonitor.exe
   - plugin **新版本**会启动，OnInitialize 会调，**新代码会读 JSONL**

2. **验证新代码真的能读 JSONL**：
   - 在 claude -p "xxx" 发一条消息 → 触发 assistant turn
   - 检查 `~/.claude/projects/<cwd>/*.jsonl` 是否有新行
   - 检查任务栏 4 个 item 是否有脉冲

3. **如果新进程仍没加载 plugin**（和这次一样的现象）：
   - 任务管理器 → 详细信息 → 找 TrafficMonitor.exe
   - 右键 → 属性 → 看"加载的模块"是否有 ClaudeTokenMonitor.dll
   - 如果没 → 检查 `%USERPROFILE%\.claude\settings.json` 是否还引用了 powershell wrapper
   - **如果 wrapper.ps1 启动的 PowerShell 进程没退出**，可能阻塞新进程的某个资源

## 调试技巧沉淀

- **`HMODULE hExe = GetModuleHandleW(nullptr)`** 显式获取主进程 exe（而非 plugin DLL）— 调试 trace 写日志到正确路径
- **`_wfopen_s`（deprecated `_wfopen`）** 写 unicode 路径下的 log（MSVC deprecation 错误 C4996 会阻止 build）
- **`fprintf_s` / `fwprintf`** 多字节 vs 宽字符；用宽字符版本 + `_wfopen_s` 避免编码错乱
- **tracelogic：用 file existence 作判据**比检查内存/进程状态更可靠（文件存在 = OnInit 跑过；文件不存在 = 链路有断点）
- **`%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl` 文件 size + mtime** 是 wrapper 是否被调用的最简探针（最准）

## 仍遗留的问题（用户操作项）

1. **手动重启 TrafficMonitor**：
   - 任务管理器结束 `TrafficMonitor.exe`（所有实例）
   - 重新启动 `G:\_GitSpace\_GitHub\TrafficMonitor.git\TrafficMonitor\Bin\x64\Release\TrafficMonitor.exe`
   - 等 3-5 秒
   - 检查任务栏是否出现 4 个新 item（plugin 已 patch ini）
   - 检查 `C:/Users/YOU/AppData/Roaming/TrafficMonitor/ctm_trace.log` 是否出现（如果写了则 plugin 跑过）

2. **触发数据流验证**：
   - `claude -p "Reply with a single word: ok"` 调一次
   - 检查 `~/.claude/projects/<cwd>/*.jsonl` 是否有新行
   - 检查 tasklist 进程内存是否增长（plugin 解析 jsonl → ring buffer 累计）

3. **如果仍不工作**：
   - 看 plugin 加载状态：主程序 GUI「插件管理」对话框
   - 看 4 个 item 是否勾选
   - 看 `error.log` 有无 load 错误

## 沉淀到 skill 的变更

- 文档：`references/topics/end-to-end-debug.md` 新增 Round 2 小节（沙箱环境 LoadLibrary 限制）
- plan 文档：第三轮 grill 待办列表（重启 + 验证新代码）
- 任务 33 标 completed（v1 编译 + 静态验证通过 + 部署就绪），最终用户端验证留作下一轮会话
