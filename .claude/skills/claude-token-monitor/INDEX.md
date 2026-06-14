# ClaudeTokenMonitor Skill — 文件索引

> 本目录是对仓库 `G:\_GitSpace\_GitHub\TrafficMonitor.git\` 中 **ClaudeTokenMonitor 插件子项目** 的需求、架构与实现状态记录。

## 目录结构与文件清单

```
claude-token-monitor/
├── SKILL.md                                ← 入口(SKILL 元信息 + 阅读路径 + 文档约定)
├── INDEX.md                                ← 本文件(目录索引)
├── references/
│   ├── overview/
│   │   ├── 00-status.md                    项目当前状态 + 待办清单
│   │   ├── 01-context.md                   术语表 (统一口径)
│   │   ├── 02-architecture.md              模块拆分 + 全局对象
│   │   ├── 03-data-flow.md                 全链路数据流 (statusline → 任务栏)
│   │   └── 04-glossary.md                  补充术语解释
│   ├── topics/
│   │   ├── data-source.md                  数据源选型 (statusline sidecar vs JSONL)
│   │   ├── plugin-contract.md              ITMPlugin / IPluginItem 实现要点
│   │   ├── custom-draw.md                  IsCustomDraw=true 自绘滚动柱形图细节
│   │   ├── wrapper-installer.md            wrapper 装 / 卸 / 备份 / 还原
│   │   ├── session-aggregation.md          3 种汇总模式 (ALL / ACTIVE / SINGLE)
│   │   ├── delta-algorithm.md              1Hz delta 计算 + 归一化 + 环形缓冲
│   │   └── risk-and-edge-cases.md          边角案例与缓解
│   └── projects/
│       └── ClaudeTokenMonitor/
│           └── 00-overview.md              C++/MFC DLL 项目结构 + 依赖
└── docs/
    └── adr/
        ├── 0001-statusline-sidecar.md      数据源: 为什么 statusline sidecar 而不是 JSONL
        ├── 0002-iscustomdraw-self-draw.md  渲染: 为什么 IsCustomDraw=true 自绘
        ├── 0003-four-items-layout.md       布局: 为什么 4 个独立 IPluginItem
        └── 0004-manual-wrapper-install.md  部署: 为什么 wrapper 在选项对话框里手动装
```

## 阅读建议

1. **新加入者** → `SKILL.md` → `references/overview/00-status.md` → `01-context.md` → `02-architecture.md` → `03-data-flow.md`
2. **改某个机制** → 直接看 `references/topics/<对应主题>.md`
3. **回顾决策理由** → `docs/adr/` 下按编号读
4. **术语不清楚** → `references/overview/01-context.md`（主表）或 `04-glossary.md`（补充）

## 文档维护规则

- **任何代码变更** → 检查 `references/` 下对应主题文档是否需要同步
- **任何架构决策** → 必须新建或更新 `docs/adr/`
- **任何术语** → 如果还没出现在 `01-context.md`，就在 `01-context.md` 补上
- **任何状态变化**（编译跑通、测试通过、部署）→ 更新 `00-status.md`
- **不要写流水账**（如"今天完成了 XX"），只写"现在已经是这样"

## 关联 skill

- [trafficmonitor-arch](../trafficmonitor-arch/SKILL.md) — TrafficMonitor 主程序本身
- [cc-switch-token-stats](../cc-switch-token-stats/SKILL.md) — 同类 token 统计项目（不同栈，作参考）
- [anysearch](../../../skills/anysearch/SKILL.md) — 调研工具（本 skill 沉淀其结果）
- [grill-with-docs-lite](../../../skills/grill-with-docs-lite/SKILL.md) — 需求评审与术语统一
