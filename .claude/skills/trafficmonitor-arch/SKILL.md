---
name: trafficmonitor-arch
description: TrafficMonitor 项目架构与代码理解知识库。覆盖主程序 (CTrafficMonitorApp)、任务栏窗口、皮肤系统、插件协议 (ITMPlugin / IPluginItem / ITrafficMonitor)、硬件监控接入 (LibreHardwareMonitor)、历史流量、设置/配置、绘制抽象 (GDI/GDI+/D2D/DComp)、i18n、更新与自启。回答关于 TrafficMonitor 架构、模块边界、数据流、插件协议、关键源码位置 (文件路径:行号) 等问题时使用。
metadata:
  trigger:
    - "TrafficMonitor"
    - "TrafficMonitor 架构"
    - "TrafficMonitor 插件"
    - "ITMPlugin"
    - "IPluginItem"
    - "ITrafficMonitor"
    - "TrafficMonitor 任务栏"
    - "CTrafficMonitorApp"
    - "CTaskBarDlg"
    - "PluginManager"
    - "CSkinFile"
    - "CUpdateHelper"
    - "OpenHardwareMonitor"
    - "LibreHardwareMonitor"
    - "PluginInterface.h"
  scope: project
---

# TrafficMonitor 项目架构知识库

这是 TrafficMonitor（Windows MFC 网速/硬件悬浮窗）仓库的代码架构理解产物。所有结论直接基于源码，以 `文件路径:行号` 形式引用，可在阅读代码时跳转核对。

## 使用方式

- **回答架构问题**：先读 `references/overview/` 建立全景认知，再按主题/项目维度跳转。
- **回答插件相关问题**：`references/topics/plugin-system.md` 是核心；接口契约在 `include/PluginInterface.h`。
- **回答"某个类/某个机制怎么工作"**：优先按主题 (`references/topics/`) 找，主题文档会交叉引用项目文档。
- **回答源码定位/行号问题**：所有文档都标 `文件路径:行号`，可直接用 Read 工具跳转。

## 不要做的事

- 不要照搬文档里的"建议/未来"语言 —— 文档只记录源码现状。
- 不要把命名/参数顺序这种末梢细节问出来 —— 不在覆盖范围。
- 不要凭记忆回答源码细节 —— 引用之前先 Read 对应文档再 Read 源码核对。

## references/ 目录结构

```
references/
├── overview/                             ← 全局视角
│   ├── 00-tech-stack.md                  技术栈、构建配置、平台/版本约束
│   ├── 01-architecture-overview.md       模块划分与全局对象
│   ├── 02-data-flow.md                   采集→缓存→绘制→插件回调 全链路
│   ├── 03-lifecycle.md                   进程生命周期与关键时序
│   └── 04-glossary.md                    术语表（统一用词口径）
├── projects/                             ← 按 VS 子项目维度
│   ├── TrafficMonitor/00-overview.md     主程序项目（MFC/Win32 桌面应用）
│   ├── OpenHardwareMonitorApi/00-overview.md  硬件监控接入项目
│   └── PluginDemo/00-overview.md         官方插件示例项目
└── topics/                               ← 按主题维度
    ├── monitor-data-collection.md        数据采集层
    ├── ui-main-dialog.md                 主悬浮窗渲染与交互
    ├── ui-taskbar-dialog.md              任务栏窗口渲染与交互
    ├── render-api.md                     绘制抽象（GDI/GDI+/D2D/DComp/DXGI）
    ├── skin-system.md                    皮肤系统
    ├── settings-and-config.md            配置与设置
    ├── plugin-system.md                  插件系统（最详细）
    ├── hardware-monitoring.md            硬件监控
    ├── history-traffic.md                历史流量统计
    ├── i18n-and-strings.md               多语言与字符串表
    ├── update-and-autostart.md           自动更新与开机自启
    └── common-utilities.md               通用工具与基础设施
```

## 阅读路径建议

1. **首次进入**：`overview/01-architecture-overview.md` → `overview/02-data-flow.md` → `overview/03-lifecycle.md`
2. **了解某个项目全貌**：`projects/<项目>/00-overview.md`
3. **了解某个主题**：`topics/<主题>.md`
4. **查找术语定义**：`overview/04-glossary.md`

## 文档约定

- 引用类名/函数名时给 `文件路径:行号`，方便 Read 跳转核对。
- 不写"应该/未来可能"——只写源码实际状态。
- 命名问题、参数顺序、方法重载细节等"不影响主体结构"的内容不在覆盖范围。
- 重要不变量和歧义点已通过 grill 追问阶段沉淀进文档（例如：插件 disabled 检查顺序、`m_minitor_lib_critical` 实际作用面、`OnMonitorInfo` 被头文件标记为"将弃用"、插件自绘 dark_mode 参数极性、IDrawCommon 后端的"静态探测 + 运行期降级"双轨机制）。