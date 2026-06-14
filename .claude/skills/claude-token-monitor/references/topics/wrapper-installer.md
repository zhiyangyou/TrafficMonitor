# Wrapper Installer

wrapper 是 PowerShell 5.1+ 脚本，被 Claude Code 主进程在每次回合通过 statusline 通道调起来。本插件的 `CStatuslineInstaller` 负责把 wrapper 写到磁盘、修改 `~/.claude/settings.json` 把 statusline 命令指向 wrapper，并在卸载时还原原始配置。

## 文件路径

| 角色 | 路径 |
|---|---|
| wrapper 脚本 | `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1` |
| sidecar JSONL | `%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl` |
| settings.json 备份 | `%APPDATA%\ClaudeTokenMonitor\settings.json.bak.<unix-ts>` |
| 原 statusline 命令备份 | `%APPDATA%\ClaudeTokenMonitor\previous-statusline.txt` |

`%APPDATA%` 在 Windows 上解析为 `C:\Users\<user>\AppData\Roaming`。

`statusline-wrapper.ps1` 由 DLL 嵌入的字符串字面量写出（`CStatuslineInstaller::Install()` 调 `WriteAllText`），不依赖外部资源文件。

## 装流程（`CStatuslineInstaller::Install()`）

1. 检查 `~/.claude/settings.json` 存在性 + 可写
   - 不存在 → 返回 `ClaudeCodeMissing`，对话框提示"未检测到 Claude Code"
   - 只读 → 返回 `SettingsReadOnly`，对话框弹错误
2. 把当前 `settings.json` 复制到 `%APPDATA%\ClaudeTokenMonitor\settings.json.bak.<unix-ts>`（多次装只保留最新一份，旧 `.bak.<ts>` 被覆盖）
3. 解析 `settings.json`（`nlohmann::json`）
4. 如果 `statusLine` 存在且 `.command` 非空 → 把 `command` 值原文写到 `%APPDATA%\ClaudeTokenMonitor\previous-statusline.txt`
5. 把 `statusLine` 改成：
   ```json
   {
     "command": "powershell -ExecutionPolicy Bypass -File <wrapper_path>",
     "originalCommand": "<原 command 值>"
   }
   ```
   - `originalCommand` 字段给 wrapper 读取，用来把 statusline 再 pipe 给原命令
   - 如果用户原本没 statusline，`originalCommand` 不写，wrapper 检测不到就静默跳过
6. 原子写回：写 `settings.json.tmp` → rename 覆盖 `settings.json`
7. 写 wrapper 脚本到 `%APPDATA%\ClaudeTokenMonitor\statusline-wrapper.ps1`（`WriteAllText`，UTF-8 BOM）
8. 创建 `%APPDATA%\ClaudeTokenMonitor\sidecar.jsonl` 空文件（如果不存在）

`Install()` 返回 `Installed` 状态码，调用方（`COptionsDlg`）刷新状态条显示"wrapper 已装"。

## 卸流程（`CStatuslineInstaller::Uninstall()`）

1. 检查 `~/.claude/settings.json` 存在
2. 解析；如果 `statusLine.command` 不指向 wrapper 路径 → 返回 `NotInstalled`（noop）
3. 读 `previous-statusline.txt`：
   - 文件存在且非空 → 把 `statusLine.command` 还原为文件内容，并删除 `statusLine.originalCommand`
   - 文件不存在或为空 → 删除整个 `statusLine` key
4. 原子写回 `settings.json.tmp` → rename
5. 不删 wrapper 脚本（保留供重新安装）
6. 不删 sidecar（保留历史数据；rotate 逻辑在 `Tick()` 里处理）

返回 `Uninstalled` 状态码。

## wrapper 脚本骨架

```powershell
# statusline-wrapper.ps1 — installed by ClaudeTokenMonitor plugin
$ErrorActionPreference = 'SilentlyContinue'
$json = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($json)) { exit 0 }

# 1. 写到 sidecar（JSONL, 一行一条）
$dir = Join-Path $env:APPDATA 'ClaudeTokenMonitor'
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$sidecar = Join-Path $dir 'sidecar.jsonl'
# 在 JSON 末尾补 wrapper_ms 字段（不改原始内容）
$wrapped = ($json.TrimEnd() -replace '}\s*$', '') + ',"wrapper_ms":' + [int64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) + '}'
# 单 write + flush + close，原子行
$stream = [System.IO.File]::Open($sidecar, 'Append', 'Write', 'Read')
$writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::UTF8)
$writer.WriteLine($wrapped)
$writer.Flush()
$stream.Close()

# 2. pipe 给原 statusline（保留用户体验）
$settings = Join-Path $env:USERPROFILE '.claude\settings.json'
$orig = $null
if (Test-Path $settings) {
    try { $orig = (Get-Content $settings -Raw | ConvertFrom-Json).statusLine.originalCommand } catch {}
}
if ($orig) {
    $tmp = [System.IO.Path]::GetTempFileName()
    Set-Content -Path $tmp -Value $json -Encoding UTF8
    cmd /c "type `"$tmp`" | $orig" 2>$null
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
}
exit 0
```

要点：
- 读 stdin：Claude Code 主进程把 statusline JSON pipe 到 wrapper 的 stdin
- `$ErrorActionPreference = 'SilentlyContinue'`：所有异常静默吃掉，wrapper 自身错误不能让 Claude Code 报错
- 末尾补 `wrapper_ms` 字段：用 regex `}\s*$` 找最后一个 `}`，去掉，补 `,"wrapper_ms":<ms>}`
- 单 `WriteLine` + `Flush` + `Close` 在 NTFS 上原子；多 writer 同时写最差情况半行 JSON 被 `nlohmann::json::parse` 拒绝 → 跳过
- pipe 给原命令：读 `statusLine.originalCommand`，临时文件 + `cmd /c "type | <orig>"` 把 stdin 重定向过去

## 手动触发

wrapper 的装卸**不在 `OnInitialize` 自动做**。`DataManager::LoadConfig()` 只调用 `CStatuslineInstaller::CheckInstalled()` 探测当前状态，结果缓存给选项对话框用。

用户在 `COptionsDlg` 里点 "Install wrapper" 按钮 → 调 `Install()`；点 "Uninstall wrapper" → 调 `Uninstall()`。状态条（`IDC_STATIC_STATUS`）显示：
- `wrapper not installed, click to install`
- `wrapper installed, click to uninstall`
- `Claude Code not detected`（Install 按钮 disable）

卸载后 wrapper 脚本本身保留在 `%APPDATA%`，用户重新点 Install 时直接覆盖写 settings.json 即可。