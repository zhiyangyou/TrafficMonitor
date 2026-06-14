# 冒烟测试 · ClaudeTokenMonitor 插件

> 范围：ClaudeTokenMonitor 插件项目（C++/MFC DLL）的最小验证流程。任何代码变更后必须跑一遍。

## 1. 流程

```
[1] 编译       →  产出 Bin\...\plugins\ClaudeTokenMonitor.dll
[2] 验证导出   →  dumpbin /EXPORTS 确认 TMPluginGetInstance 存在
[3] 验证架构   →  dumpbin /HEADERS 确认 machine = x64（与主程序匹配）
```

任何一步失败 → 冒烟测试不通过，必须修复后重跑。

## 2. 命令（Windows + MSBuild 14.33）

### 2.1 编译（Release x64）

```bash
cd "G:/_GitSpace/_GitHub/TrafficMonitor.git"
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  "ClaudeTokenMonitor/ClaudeTokenMonitor.vcxproj" \
  -t:Build \
  -p:Configuration=Release \
  -p:Platform=x64 \
  -v:minimal
```

**通过判据**：输出最后一行含 `-> G:\_GitSpace\_GitHub\TrafficMonitor.git\ClaudeTokenMonitor\Bin\x64\Release\plugins\ClaudeTokenMonitor.dll`，且没有 `error C` 或 `error MSB`。

**已知非阻塞警告**（可接受）：

| 警告 | 来源 | 处理 |
| --- | --- | --- |
| `C4819: 当前代码页(936)中无法表示该字符` | 源文件含中文注释 | 已在 v1 修完（去掉 §× 中文符号）。新增代码如再触发，可加 `/utf-8` 编译选项忽略 |
| `The contents of <filesystem> are available only with C++17 or later.` | `StatuslineInstaller.cpp:7` | 工具集 v143 默认 C++17/20，可忽略 |
| `warning C4244`（OpenHardwareMonitorApi 项目） | 主程序 OpenHardwareMonitor 模块 | **不是本插件的问题**，无关 |

### 2.2 验证导出

```bash
cd "G:/_GitSpace/_GitHub/TrafficMonitor.git/ClaudeTokenMonitor/Bin/x64/Release/plugins"
"C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.33.31629/bin/Hostx64/x64/dumpbin.exe" \
  -EXPORTS ClaudeTokenMonitor.dll
```

**通过判据**：输出含 `TMPluginGetInstance = TMPluginGetInstance`（即 RVA + 名称行）。

> 注：在 bash 里要用 `-EXPORTS` 而非 `/EXPORTS`，MSYS 会把 `/EXPORTS` 当成路径。

### 2.3 验证架构

```bash
cd "G:/_GitSpace/_GitHub/TrafficMonitor.git/ClaudeTokenMonitor/Bin/x64/Release/plugins"
"C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.33.31629/bin/Hostx64/x64/dumpbin.exe" \
  -HEADERS ClaudeTokenMonitor.dll
```

**通过判据**：输出含 `machine (x64)`。

## 3. 失败模式速查

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| `error MSB1009: 项目文件不存在` | bash 路径被 `\` 吃成 `ClaudeTokenMonitorClaudeTokenMonitor.vcxproj` | 用正斜杠 `ClaudeTokenMonitor/ClaudeTokenMonitor.vcxproj` |
| `error C2601: 函数定义非法` 后多个方法 | 中文 `§` / `×` 字符把 MSVC 词法分析搞乱 | 替换为 ASCII (`section` / `x`) |
| `error RC2104: undefined keyword` | `.rc` 文件用 GBK（`#pragma code_page(936)`）解析中文失败 | 改为 `#pragma code_page(65001)` (UTF-8) |
| `error C1083: PluginInterface.h: No such file` | `<PluginInterface.h>` 找不到 | 检查 vcxproj `IncludePath` 是否含 `$(ProjectDir)..\include` |
| `link.exe 找不到 uuid.lib / msvcrt.lib` | 缺 MSVC SDK 环境变量 | 跑 `vcvars64.bat` 再用 MSBuild |
| `fatal error C1075: 缺少匹配的 '}'` | `IMPLEMENT_DYNAMIC` 宏展开失败（多半因为 include 顺序或 MFC 头缺失） | 确认 cpp 里 include 顺序：`pch.h` → `afxdialogex.h` → 自己的 .h → `IMPLEMENT_DYNAMIC` |

## 4. 输出位置

**当前路径**（单独构建 vcxproj，未走 sln）：
```
G:\_GitSpace\_GitHub\TrafficMonitor.git\ClaudeTokenMonitor\Bin\x64\Release\plugins\ClaudeTokenMonitor.dll
```

> 注意：单独 MSBuild vcxproj 时，`$(SolutionDir)` 为空，所以 OutDir 退化为子项目根 `Bin\`。要让 DLL 落到主程序 `Bin\...` 下，必须**通过 sln 整体构建**。本冒烟测试**不依赖** DLL 在主程序 `Bin\`，因为还没法在沙箱里启动 TrafficMonitor.exe 主程序做集成测试。

## 5. 集成测试（暂未自动化）

要把插件加载到 TrafficMonitor 主程序并验证 4 个 item 出现，需要：

1. 整体构建 sln（一次性产出 TrafficMonitor.exe + plugins\ClaudeTokenMonitor.dll）
2. 启动 `TrafficMonitor.exe`
3. 打开"插件管理"，确认 4 个 item（Token In / Cache Write / Cache Read / Token Out）出现
4. 任务栏右键 → 显示项 → 勾选这 4 个

集成测试**不在**本冒烟测试范围（沙箱里没法启动 GUI）。本冒烟测试的判据是"DLL 文件存在 + 导出正确 + 架构匹配"——这是 **LoadLibrary 成功** 的最小必要条件。

## 6. 历史执行记录

| 日期 | 结果 | 备注 |
| --- | --- | --- |
| 2026-06-14 | ✅ PASS | 首次冒烟测试通过。MSBuild 14.33 / VS 2022 Community / Release x64 / 40KB DLL |

## 7. 引用

- 主程序加载插件：`TrafficMonitor/PluginManager.cpp:35-135`（`LoadPlugins`）
- DLL 导出函数约定：`include/PluginInterface.h:426-431`
- 插件项目结构：`ClaudeTokenMonitor/ClaudeTokenMonitor.vcxproj`
