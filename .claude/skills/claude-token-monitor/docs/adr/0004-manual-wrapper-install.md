---
status: accepted
date: 2026-06-14
---

# 0004 — Wrapper 通过选项对话框手动装/卸

statusline wrapper 会修改 `~/.claude/settings.json`——这是用户的 Claude Code 配置文件，不是插件自己域内的文件。决策是 wrapper **不**在插件 `OnInitialize` 里自动部署，而是通过 `COptionsDlg` 的 **Install / Uninstall 按钮**手动触发；安装前备份原 `settings.json` 到 `%APPDATA%\ClaudeTokenMonitor\settings.json.bak.<unix-ts>`，卸载时还原。

静默改用户配置 = 隐式行为 = 信任问题。用户装一个"任务栏网速插件"绝不应该预期到 `~/.claude/settings.json` 被改写。手动安装让用户能**先看 installer 改什么、再决定接受**。此外，反病毒/EDR 软件经常拦截/告警修改 `settings.json` 的进程，明确的用户意图能减少误报。

备份策略：每次 Install 前备份当前 `~/.claude/settings.json` 到 `%APPDATA%\ClaudeTokenMonitor\settings.json.bak.<unix-ms>`，并把当时的 `statusLine.command` 另存到 `previous-statusline.txt`；Uninstall 时读这个文件还原 `statusLine`，或在没有原值时整个删掉 `statusLine` 键。

## Considered Options

- **A. 静默自动装（`OnInitialize` 第一次跑时装）** — 否决。隐式修改用户配置，违反最小惊讶原则；EDR 容易拦。
- **B. 首次启动弹 `ITrafficMonitor::ShowNotifyMessage` 提示用户同意** — 折中但仍是"半自动"：用户点×就装、点忽略就忘，意图不强烈，且通知消息很容易被错过。
- **C. 在选项对话框里手动点 Install/Install 按钮** — 采纳。意图清晰、可撤销、有 backup/restore；用户打开 Options 时已经主动寻求插件控制面板。

## Consequences

- 用户必须**主动**打开插件 Options → 点 Install wrapper，首装时任务栏没数据是正常现象。
- `CStatuslineInstaller::CheckInstalled()` 在 `OnInitialize` 里跑一次，结果（已装/未装/Claude Code 缺失）缓存给对话框显示；Claude Code 没装时 Install 按钮 disable。
- Install 流程幂等：检测到 `statusLine.command` 已经指向我们的 wrapper 时不重复装、不重复备份（避免 `.bak` 文件无限增长）。
- wrapper 脚本以**字符串字面量嵌入 DLL**（参考 `PluginDemo` 的模式），Install 时写到 `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1`；卸载时**不删**脚本（用户可能想再装回来）。
- Uninstall 必须有 `previous-statusline.txt` 兜底，否则卸载后用户的 statusline 配置会被吃掉——这条逻辑必须加单元测试。