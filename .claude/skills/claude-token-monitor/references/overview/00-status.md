# 00 · 项目当前状态

> 范围：ClaudeTokenMonitor 插件项目从计划到落地的现状盘点。所有"是否完成"按源码实际状态记录。

## 1.1 计划与文档

| 项目 | 路径 | 状态 |
| --- | --- | --- |
| 主计划文件 | `C:\Users\YOU\.claude\plans\anysearch-cc-switch-token-stats-traffic-mossy-flask.md` | 已存在；记录 7 项架构决策 + 端到端验证 7 阶段 |
| 本 skill SKILL.md | `.claude/skills/claude-token-monitor/SKILL.md` | 已存在；覆盖需求/架构/接口/数据源/风险 5 主题 |
| 本目录 overview 文档 | `.claude/skills/claude-token-monitor/references/overview/` | 5 份现状记录（00-04），仅记现状 |

## 1.2 源码目录

| 路径 | 状态 |
| --- | --- |
| `G:\_GitSpace\_GitHub\TrafficMonitor.git\ClaudeTokenMonitor\` | **已创建** — 28 个文件（vcxproj / .filters / framework.h / pch.h / pch.cpp / resource.h / 8 个头 / 8 个实现 / .rc + 占位 json.hpp） |
| `ClaudeTokenMonitor/include/nlohmann/json.hpp` | **占位 stub**（用户需替换为真 nlohmann/json.hpp v3.11.3） |
| Wrapper 脚本（`statusline-wrapper.ps1`） | **嵌入字符串** 已在 `StatuslineInstaller.cpp:14-45` 完成；CStatuslineInstaller::Install() 是 TODO stub，未真正写出文件 |

## 1.3 解决方案集成

| 集成点 | 状态 |
| --- | --- |
| `TrafficMonitor.sln` 包含 `ClaudeTokenMonitor.vcxproj` | **已集成**（GUID `{6A8A4F7E-1D2C-4B3D-9E5F-7A8B9C0D1E2F}`） |
| `TrafficMonitor_Lite.scx` 包含 `ClaudeTokenMonitor.vcxproj` | **未做** |
| vcxproj.filters 文件 | **已创建** |
| 项目 GUID 已注册 | **是** |

## 1.4 编译产出

| 产物 | 期望路径 | 存在 |
| --- | --- | --- |
| `ClaudeTokenMonitor/Bin/x64/Release/plugins/ClaudeTokenMonitor.dll` | 单独构建 vcxproj 时的 OutDir | **是**（40KB, 2026-06-14 首次冒烟测试通过） |
| `Bin\Release\x64\plugins\ClaudeTokenMonitor.dll` | sln 整体构建时才会产出 | 否（未跑 sln 整体构建） |
| `Bin\Debug\x64\plugins\ClaudeTokenMonitor.dll` | 同上 | 否 |
| `Bin\Release\Win32\plugins\ClaudeTokenMonitor.dll` | 同上 | 否 |

> 冒烟测试命令与判据见 `references/topics/smoke-test.md`。

## 1.5 端到端测试

| 阶段 | 计划文件定义 | 当前状态 |
| --- | --- | --- |
| Phase 1 — 编译（3 平台 × 2 配置） | 跑通 vs2022 build | 未开始 |
| Phase 2 — 插件加载（4 个 item 出现在「插件管理」） | DLL 放入 `plugins\` + 启动 TrafficMonitor | 未开始 |
| Phase 3 — wrapper round-trip（settings.json 被改写、sidecar.jsonl 多出一行） | Options 对话框 Install + 跑消息 | 未开始 |
| Phase 4 — 端到端可视（柱形图每 1s 刷新） | 任务栏 4 item 实时显示 | 未开始 |
| Phase 5 — 汇总模式（ALL / ACTIVE / SINGLE） | 同时开 2 session 切模式验证 | 未开始 |
| Phase 6 — 卸载（settings.json 还原） | Options → Uninstall 验证 | 未开始 |
| Phase 7 — 边角（坏 JSON / 删 sidecar / 重启 / DPI） | 4 项异常路径 | 未开始 |

## 1.6 4 类 token 监控的就绪度

| Token 类别 | 数据源（statusline JSON 字段） | 解析逻辑实现 |
| --- | --- | --- |
| input | `context_window.current_usage.input_tokens` | 未实现（`CPerSessionAccumulator::OnStatuslineUpdate` 函数不存在） |
| cache_creation | `context_window.current_usage.cache_creation_input_tokens` | 未实现 |
| cache_read | `context_window.current_usage.cache_read_input_tokens` | 未实现 |
| output | `context_window.current_usage.output_tokens` | 未实现 |

## 1.7 已知 blocker

- 源码目录 `ClaudeTokenMonitor/` 整个缺失 → 所有后续阶段都被该 blocker 阻塞
- sln 未注册新项目 → 编译链路未接通
- Wrapper PowerShell 脚本、DLL 嵌入字符串、statusline wrapper 安装/卸载流程全部未实现

## 1.8 已对齐的架构决策（来自主计划，不在本文档讨论范围）

| # | 决策点 | 选择 | 来源 |
| --- | --- | --- | --- |
| 1 | 数据源 | statusline sidecar（读 wrapper 写入的 JSONL） | 计划文件 §已对齐的架构决策 |
| 2 | 监控范围 | 3 种汇总模式（合并 / 仅活动 / 单个 session） | 同上 |
| 3 | UI 形态 | 4 个独立 IPluginItem，`IsCustomDraw=true` 自绘滚动柱形图 | 同上 |
| 4 | 技术栈 | C++/MFC DLL，与 PluginDemo 一致 | 同上 |
| 5 | cache 折算 | 4 类分别计 | 同上 |
| 6 | wrapper 安装 | 选项对话框里手动装/卸（带 backup/restore） | 同上 |
| 7 | 速度定义 | 过去 1 秒的 delta tokens | 同上 |

## 1.9 与本节相关的关键文件索引

- 主计划：`C:\Users\YOU\.claude\plans\anysearch-cc-switch-token-stats-traffic-mossy-flask.md`
- 本 skill SKILL.md：`.claude/skills/claude-token-monitor/SKILL.md`
- 计划中引用的主程序文件（实现时的对接面，非本项目源码）：`include/PluginInterface.h`、`TrafficMonitor/PluginManager.cpp:35-135`、`TrafficMonitor/TaskBarDlg.cpp:381-485`、`TrafficMonitor/TaskBarDlg.cpp:1350-1427`、`TrafficMonitor/TrafficMonitorDlg.cpp:1500-1519`
