# cc-switch Token 统计 Skill

> 本目录是对仓库 `G:\_GitSpace\_GitHub\cc-switch`（v3.16+）中 **token 统计子系统** 的实现原理记录。

## 目录结构

```
cc-switch/
├── SKILL.md                              ← 入口(原理总览、关键概念、踩坑指南、文件清单)
├── INDEX.md                              ← 本文件
└── references/
    ├── parser-and-calculator.md          协议解析与成本计算(parser.rs / calculator.rs / logger.rs)
    ├── persistence.md                    SQLite schema、Rollup 算法、跨源去重、Backfill
    ├── frontend-ui.md                    前端组件树、React Query、事件桥、筛选语义
    └── glossary.md                       术语表(cache-inclusive / fresh-input / effective filter 等)
```

## 阅读建议

1. **先读 [SKILL.md](./SKILL.md)** —— 原理总览 + 数据流全图 + 关键概念 + 踩坑指南
2. **想了解某个细节** → 看 `references/` 下的对应子文档
3. **遇到不熟悉的术语** → 先查 [glossary.md](./references/glossary.md)

## 文档约定

- 所有结论直接来自源码 + 3 个 SubAgent 并行探索 + 关键文件手读核验。
- 引用代码位置用 `文件路径:行号` 形式以便跳转。
- 不写"应该/未来可能/将要"，只写"现在已经是这样"。
- 命名、参数顺序、注释风格等不影响主体结构的内容不在覆盖范围。
