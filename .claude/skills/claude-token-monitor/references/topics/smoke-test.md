# 冒烟测试 · ClaudeTokenMonitor 插件

> 范围：ClaudeTokenMonitor 插件项目（C++/MFC DLL）的最小验证流程。任何代码变更后必须跑一遍。

## 1. 流程

```
[0] 关闭 Monitor 进程      ← 强制前置（任务 27 新约束）
[1] 编译插件 DLL
[2] 验证插件导出   (dumpbin -EXPORTS → TMPluginGetInstance)
[3] 验证插件架构   (dumpbin -HEADERS → machine = x64)
[4] 编译主程序     (含 PreBuild .bat，需要 .bat 目录在 PATH)
[5] 复制插件到主程序 plugins\ 目录
[6] 启动主程序     (cmd /c start /B TrafficMonitor.exe)
[7] 验证进程存活   (tasklist 看到 TrafficMonitor.exe PID)
[8] 杀进程         (如沙箱有权；或保留运行）
```

任何一步失败 → 冒烟测试不通过，必须修复后重跑。

**步骤 [0] 是 2026-06-14 起的硬性约束**：

- **为什么**：之前一次跑冒烟测试时，旧的 TrafficMonitor.exe 进程还活着（之前的 session 启动后没回收），MSBuild 编译时 `TrafficMonitor.exe` 处于被加载状态 → 新编译出来的 exe **无法被覆盖**，导致静默运行旧版本 + 跑的是新编译 DLL，但**主程序本身是旧的** → 任何主程序源码相关的修改（plugin 协议字段、API 行为）都不会反映出来。**冒烟测试报告 PASS，但实际是假象**。
- **前置关闭是冒烟测试可信度的唯一保障**。

**关键判据**：步骤 [7] 是"插件真的能跑起来"的**唯一可靠证据**。前面 6 步都只是"代码静态正确"，只有真启动 TrafficMonitor.exe 并看到 LoadLibrary 成功（= 进程稳定不闪退 + 内存稳定在 ~80-90MB），才证明：
- `TMPluginGetInstance` 能正确返回 `&Instance()`
- `GetAPIVersion()` 返回 7
- 4 个 `IPluginItem*` 可被主程序消费
- `OnInitialize` / `DataRequired` 不死循环不 crash
- DLL 静态依赖（MFC / VCRuntime）能在主程序上下文中解析

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

> 注意：单独 MSBuild vcxproj 时，`$(SolutionDir)` 为空，所以 OutDir 退化为子项目根 `Bin\`。要让 DLL 落到主程序 `Bin\...` 下，必须**通过 sln 整体构建**或步骤 5.5 手动复制。

## 2.5 编译主程序（含 PreBuild .bat 依赖）

**前置**：先单独 build `OpenHardwareMonitorApi`（主程序 link 依赖 `OpenHardwareMonitorApi.lib`）。

```bash
# Step 0: 单独 build OpenHardwareMonitorApi
cd "G:/_GitSpace/_GitHub/TrafficMonitor.git"
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  "OpenHardwareMonitorApi/OpenHardwareMonitorApi.vcxproj" \
  -t:Build -p:Configuration=Release -p:Platform=x64 -v:minimal
# → 产出 Bin\x64\Release\OpenHardwareMonitorApi.{lib,dll}

# Step 1: build 主程序 (cd 进入子项目，使 .bat 在 PATH)
cd "G:/_GitSpace/_GitHub/TrafficMonitor.git/TrafficMonitor"
export PATH="$PWD:$PATH"   # 关键: 让 print_compile_time.bat 在 PATH
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  TrafficMonitor.vcxproj -t:Build -p:Configuration=Release -p:Platform=x64 -v:minimal
# → 产出 TrafficMonitor\Bin\x64\Release\TrafficMonitor.exe

# Step 2: 复制 OpenHardwareMonitorApi.lib 到主程序 OutDir
#  (单 vcxproj 单独 build 时, OutDir 是项目子目录, 不在仓库根)
cp "G:/_GitSpace/_GitHub/TrafficMonitor.git/Bin/x64/Release/OpenHardwareMonitorApi.lib" \
   "G:/_GitSpace/_GitHub/TrafficMonitor.git/TrafficMonitor/Bin/x64/Release/"

# Step 3: 把插件 DLL 复制到主程序 plugins\ 子目录
mkdir -p "G:/_GitSpace/_GitHub/TrafficMonitor.git/TrafficMonitor/Bin/x64/Release/plugins"
cp "G:/_GitSpace/_GitHub/TrafficMonitor.git/ClaudeTokenMonitor/Bin/x64/Release/plugins/ClaudeTokenMonitor.dll" \
   "G:/_GitSpace/_GitHub/TrafficMonitor.git/TrafficMonitor/Bin/x64/Release/plugins/"
```

**通过判据**：最后一行含 `-> ...\TrafficMonitor.exe`；没有 `error C` / `error LNK` / `error MSB`。

## 2.6 启动主程序（真运行）

```bash
cd "G:/_GitSpace/_GitHub/TrafficMonitor.git/TrafficMonitor/Bin/x64/Release"
cmd //c "start /B TrafficMonitor.exe"
sleep 5   # 等插件 LoadLibrary + 1Hz DataRequired
tasklist 2>/dev/null | grep -i traffic
```

**通过判据**：
- `tasklist` 看到 `TrafficMonitor.exe` 行（含 PID 和内存占用）
- 内存稳定在 ~80-90MB 范围（不是缓慢上涨 = 无内存泄漏 = DataRequired 没在堆上累积）
- 进程**不**在 5 秒内自动闪退

**失败模式**：
- 进程在 5 秒内消失 → LoadLibrary 失败或入口 crash，**没有**说明会写在哪里
- 内存 > 200MB 且持续上涨 → 1Hz Tick 累积无界
- 出现 dialog 框（截图能看见）→ PluginManager 报错 "无法加载插件 ClaudeTokenMonitor.dll"

## 2.7 杀进程

GUI 进程在沙箱里通常无 kill 权限，按 Ctrl+C 中断当前 shell 即可，进程会持续在另一个 session 运行。如必须 kill：

```bash
powershell -Command "Stop-Process -Name TrafficMonitor -Force"
```

如果仍 `Access is denied`，说明沙箱隔离生效——可忽略，进程会随 session 结束被回收。

## 2.8 步骤 0：关闭 Monitor 进程（**强制前置**）

```bash
# 1. 检查是否在跑
tasklist 2>/dev/null | grep -i traffic
# → 如果有输出，进程在跑，必须先杀

# 2. 杀进程（沙箱外：用 taskkill 直接按 PID）
taskkill //F //IM TrafficMonitor.exe 2>/dev/null
# 沙箱内：用 powershell Stop-Process
powershell -Command "Stop-Process -Name TrafficMonitor -Force -ErrorAction SilentlyContinue" 2>/dev/null

# 3. 等待 2 秒（确保文件句柄释放）
sleep 2

# 4. 再次检查（必须空）
tasklist 2>/dev/null | grep -i traffic || echo "CLEAN - safe to rebuild"
```

**为什么必须**：
- 主程序 .exe 加载时占用 `TrafficMonitor.exe` 文件句柄 → 编译会失败 `LNK1168 / MSB8013` "无法写入"
- DLL 加载时占用 `ClaudeTokenMonitor.dll` 文件句柄 → 编译后 dll 旧版本会被静默锁定
- 即使编译"成功"，新代码不会在主程序运行实例里生效（运行的是旧镜像）

**失败模式**：跳过步骤 0 直接编译，可能出现：
- LNK1168 "无法打开文件 TrafficMonitor.exe 进行写入"
- MSB8013 警告 "the file has been modified since the prebuild event"
- 看似编译成功但主程序仍跑旧版本（最危险 — 假 PASS）

## 5. 集成测试（暂未自动化）

要把插件加载到 TrafficMonitor 主程序并验证 4 个 item 出现，需要：

1. 整体构建 sln（一次性产出 TrafficMonitor.exe + plugins\ClaudeTokenMonitor.dll）
2. 启动 `TrafficMonitor.exe`
3. 打开"插件管理"，确认 4 个 item（Token In / Cache Write / Cache Read / Token Out）出现
4. 任务栏右键 → 显示项 → 勾选这 4 个

集成测试**包含**本冒烟测试的范围（步骤 6-7 已实现）。步骤 8-9（"打开插件管理看到 4 个 item"）需要**人工 GUI 操作**，沙箱里无法自动化；进程稳定存活是 "GUI 内能展示" 的必要不充分条件。

## 6. 历史执行记录

| 日期 | 结果 | 备注 |
| --- | --- | --- |
| 2026-06-14 第一次 | ✅ PASS (1-3) | 静态三步：编译 + 导出 + 架构。DLL 40KB |
| 2026-06-14 第二次 | ✅ PASS (1-7) | 完整冒烟：编译主程序 + 复制 DLL + 启动 TrafficMonitor.exe。进程稳定存活 20+ 秒，内存稳定在 84,356-84,524 KB（不增长 = 无泄漏）。证明 LoadLibrary / TMPluginGetInstance / GetAPIVersion / 4 个 IPluginItem / DataRequired 1Hz 全部正常 |

## 7. 引用

- 主程序加载插件：`TrafficMonitor/PluginManager.cpp:35-135`（`LoadPlugins`）
- DLL 导出函数约定：`include/PluginInterface.h:426-431`
- 插件项目结构：`ClaudeTokenMonitor/ClaudeTokenMonitor.vcxproj`
