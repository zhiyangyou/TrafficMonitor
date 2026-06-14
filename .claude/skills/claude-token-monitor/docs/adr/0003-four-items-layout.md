---
status: accepted
date: 2026-06-14
---

# 0003 — 布局用 4 个独立 IPluginItem

Claude Code 的 token 计费有 4 类——裸 `input_tokens`、`cache_creation_input_tokens`、`cache_read_input_tokens`、`output_tokens`——它们**价格不同**（cache_read 通常是 input 的 0.1x，cache_creation 是 ~1.25x），用户明确要求**分别计、分别看**。决策是 `CClaudeTokenMonitorPlugin::GetItem(index)` 返回 4 个独立 `CTokenItem`，每类一个、各占任务栏一格。

4 个 item **共享**同一份 sidecar 数据 + 同 1Hz Tick（`DataManager::Tick()` 只跑一次），但各自的 `GetResourceUsageGraphValue` / `GetItemValueText` 独立——与 TrafficMonitor 内置显示项（CPU / 内存 / GPU / 网速 各一格）的概念模型完全一致。用户可在 TrafficMonitor 主程序设置里**独立**勾选/取消、独立拖位置、独立调每个 item 的颜色（在插件 Options 里）。

## Considered Options

- **A. 1 个 IPluginItem 内部画 4 根柱** — 否决。失去 item 级独立开/关：用户想关掉 cache_read 单独看 input/output 就做不到；与主程序"每个显示项一格"的统一心智模型也冲突。
- **B. 4 个独立 IPluginItem** — 采纳。每类一格，独立开/关、独立位置、独立颜色；数据从同一个 `DataManager` 单例拉。
- **C. 2 个 IPluginItem（"Token In"含 3 根柱 + "Token Out"1 根柱）** — 否决。视觉上 cache_creation 与 cache_read 都是 cache 类，混进 input 槽会让"input 价格"的估算失真；语义也不清。

## Consequences

- 任务栏横向被占 **4 格**，对小屏任务栏（垂直 / 拥挤场景）压力较大；用户可在主程序设置里关掉不关心的 1-3 个。
- 每个 item 必须返回**稳定唯一**的 `GetItemId()`（如 `L"{B3E1F0A2-...}-input"`）——主程序用它做 item 持久化 key，重命名/漂移会导致用户配置（颜色、开关状态）丢失。
- 4 个 item 共享 `CRingBuffer` 的归一化基线（`m_max_in` / `m_max_cc` / `m_max_cr` / `m_max_out` 各自独立），所以 max 状态也要 4 份；`DataManager` 里得是 4 个独立字段而不是 `std::array<float, 4>`。
- `GetItem(0..3)` 顺序固定为：input → cache_creation → cache_read → output；写文档时也要遵守。