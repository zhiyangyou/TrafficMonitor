---
name: claude-token-monitor
description: ClaudeTokenMonitor 插件项目（TrafficMonitor 的子项目）的需求、架构、接口契约、数据源、风险记录。覆盖：插件如何在 4 个 IPluginItem 上用 IsCustomDraw 自绘 4 路滚动柱形图、statusline wrapper 拦截 Claude Code Desktop 的真实 token 流量、1Hz delta 计算、3 种汇总模式。回答关于"ClaudeTokenMonitor 怎么读数据/怎么画图/为什么这样设计/接下来要做什么"等问题时使用。
metadata:
  trigger:
    - "ClaudeTokenMonitor"
    - "ClaudeTokenMonitor 插件"
    - "claude token monitor"
    - "Claude Code 任务栏"
    - "Token 任务栏"
    - "statusline wrapper"
    - "sidecar"
    - "Token 滚动柱形图"
    - "Token 消耗速度"
  scope: project
---

# ClaudeTokenMonitor Skill

本目录是对仓库 `G:\_GitSpace\_GitHub\TrafficMonitor.git\` 中 **ClaudeTokenMonitor 插件子项目** 的需求、架构与实现状态记录。

## 当前状态

| 项 | 状态 |
|---|---|
| 计划文件 | `C:\Users\YOU\.claude\plans\anysearch-cc-switch-token-stats-traffic-mossy-flask.md`（已审批，待实施） |
| 源码目录 | `TrafficMonitor.git/ClaudeTokenMonitor/`（**尚未创建**，本 skill 建好后会创建骨架） |
| 编译产出 | 未开始 |
| 端到端测试 | 未开始 |

## 阅读路径

1. **首次进入** → `references/overview/00-status.md` 看项目当前在哪一步
2. **理解领域概念** → `references/overview/01-context.md`（术语表，统一口径）
3. **理解整体架构** → `references/overview/02-architecture.md` + `03-data-flow.md`
4. **查找某个机制** → `references/topics/` 下对应主题
5. **理解某个决策** → `docs/adr/` 下对应 ADR
6. **理解为什么这样设计** → `references/topics/data-source.md`（数据源选型）和 `references/topics/plugin-contract.md`（接口契约）

## 目录结构

```
claude-token-monitor/
├── SKILL.md                                ← 本文件
├── INDEX.md                                ← 文件索引
└── references/
    ├── overview/
    │   ├── 00-status.md                    当前状态与待办
    │   ├── 01-context.md                   术语表（CONTEXT.md 等价物）
    │   ├── 02-architecture.md              模块拆分与全局对象
    │   ├── 03-data-flow.md                 statusline → wrapper → sidecar → plugin → 任务栏 全链路
    │   └── 04-glossary.md                  补充术语
    ├── topics/
    │   ├── data-source.md                  statusline sidecar 方案 / 为什么不读 JSONL
    │   ├── plugin-contract.md              ITMPlugin/IPluginItem 实现要点
    │   ├── custom-draw.md                  IsCustomDraw=true 自绘滚动柱形图
    │   ├── wrapper-installer.md            wrapper 装/卸/备份/还原
    │   ├── session-aggregation.md          3 种汇总模式（ALL / ACTIVE / SINGLE）
    │   ├── delta-algorithm.md              1Hz delta 计算与归一化
    │   └── risk-and-edge-cases.md          边角案例与缓解
    └── projects/
        └── ClaudeTokenMonitor/
            └── 00-overview.md              C++/MFC DLL 项目结构
└── docs/
    └── adr/
        ├── 0001-statusline-sidecar.md      数据源选型
        ├── 0002-iscustomdraw-self-draw.md  自绘 vs 复用主程序
        ├── 0003-four-items-layout.md       4 个 IPluginItem
        └── 0004-manual-wrapper-install.md  wrapper 手动装卸
```

## 文档约定

- **只记现状，不记流水账**：不写"我们做了什么、接下来要做什么"，只写"现在已经是这样"
- **引用代码位置用 `文件路径:行号`**，方便 Read 跳转
- **不写"应该/未来可能/将要"**：写"已经是这样"
- **命名、参数顺序、注释风格**等不影响主体结构的内容不在覆盖范围
- **重要不变量**和**歧义点**必须显式写出来（例如：sidecar 不存在时柱形图行为、wrapper 未装时 DataRequired 行为、first_seen 时 delta=0）
- **决策变更时同步更新**：代码改了 → 改 references/；决策变了 → 改 docs/adr/ + INDEX.md

## 与其他 skill 的关系

- **trafficmonitor-arch**：回答 TrafficMonitor 主程序本身的架构问题。本 skill 只关注 ClaudeTokenMonitor 子项目，假设主程序知识已沉淀在 trafficmonitor-arch。
- **cc-switch-token-stats**：同源类比项目，token 统计的另一种实现（Rust + Tauri），与本项目无直接代码复用关系，仅作设计思路参考。
- **anysearch**：调研阶段用，调研完成后结果沉淀到本 skill 的 references/，不再每次重新查。
- **grill-with-docs-lite**：做需求评审和决策追问时使用，追问结果沉淀到 CONTEXT.md（等价于本 skill 的 `references/overview/01-context.md`）和 ADR。
