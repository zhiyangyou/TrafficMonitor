# 多语言与字符串表

> 覆盖 `CStrTable`（字符串资源加载器）、`LanguageInfo`（BCP-47 标识的载体）、加载流程、`language/` 目录约定、插件通过 `ITrafficMonitor::GetStringRes` 获取主程序字符串、以及 README/Help 双语文档的关系。

## 1. 字符串加载器：`CStrTable`

文件：`TrafficMonitor/StrTable.{h,cpp}`。

### 1.1 角色与数据结构

- **不依赖 MFC 字符串表**：所有翻译字符串都装在 ini 风格的文件里，分为 `[general]` / `[text]` / `[menu]` 三个 section（`StrTable.cpp:140`），可被插件、外部 `.ini` 文件扩展；
- `m_string_table` 是 `map<section, map<key, value>>`（`StrTable.h:43`）；
- `m_language_list`：枚举到的所有 `LanguageInfo`（按 bcp_47 升序，`StrTable.cpp:66`）；
- `m_language_info`：当前激活语言。

### 1.2 加载流程：`Init()`（`StrTable.cpp:61`）

按顺序做三件事：

1. **枚举 EXE 内置翻译资源**
   `EnumResourceLanguages(NULL, _T("TEXT"), MAKEINTRESOURCE(IDR_LANGUAGE), EnumResLangProc, ...)`（`StrTable.cpp:64`）。`IDR_LANGUAGE` 是一个 `TEXT` 类型资源，每个语言版本有独立条目；`EnumResLangProc` 把每条资源反序列化为 `ini`，提取 `[general]` 中的 `BCP_47 / DISPLAY_NAME / TRANSLATOR / TRANSLATOR_URL / DEFAULT_FONT`，构造 `LanguageInfo` 推入 `m_language_list`。
2. **载入默认 + 当前语言字符串**
   先读 `IDR_LANGUAGE_DEFAULT`（`StrTable.cpp:71`）—— 这是**兜底表**，缺翻译时回退；再读 `IDR_LANGUAGE`（`StrTable.cpp:75`），用其 `[general]` 段填充 `m_language_info`。
3. **扫描外部 `language/*.ini`**
   `language_dir = theApp.m_module_dir + L"language"`（DEBUG 下用相对路径 `./language`）（`StrTable.cpp:82-86`），用 `CCommon::GetFiles` 枚举全部 `*.ini`，逐个：
   - 读出 `[general]` 构造 `LanguageInfo`；
   - `language_id = LocaleNameToLCID(bcp_47.c_str(), 0)`（`StrTable.cpp:94`）—— 把 BCP-47 转 LCID，喂给线程语言切换；
   - 若 `language_info == theApp.m_general_data.language`（**完整相等**，不只是 bcp_47），则**用外部 ini 覆盖**已加载的字符串（`StrTable.cpp:96-100`）；
   - 若 `m_language_list` 中还没有它，则追加进去（`StrTable.cpp:103-105`）。

### 1.3 取值 API

- `LoadText(key)` → 查 `[text]` section；
- `LoadTextFormat(key, {…})` → 替换 `<%1%>`、`<%2%>` … 占位符（`StrTable.cpp:188-194`）；
- `LoadMenuText(key)` → 查 `[menu]` section；
- `LoadText(key, section)` → 任意 section（插件用此入口）。

未命中时 `ASSERT(false)` 并返回空串（`StrTable.cpp:175`）—— 但因为第 2 步先读了 `IDR_LANGUAGE_DEFAULT`，任何"主程序定义过的键"几乎不会缺失。

## 2. `LanguageInfo` 与配置文件持久化

`TrafficMonitor/CommonData.h:171`。字段：`display_name / bcp_47 / default_font_name / translator / translator_url / language_id`。

### 2.1 序列化：`toConfigString / fromConfigString`（`CommonData.cpp:296-318`）

格式：`bcp_47|display_name|translator`（`CommonData.cpp:298`），按 `|` 切分。

调用面：
- `CTrafficMonitorApp::LoadLanguageConfig()`（`TrafficMonitor.cpp:59`）：从 `m_config_path` 的 `[general] language` 字段读出字符串，调 `fromConfigString`；
- `SaveConfig()`（`TrafficMonitor.cpp:297`）：写回 `ini.WriteString(_T("general"), _T("language"), m_general_data.language.toConfigString());`。

### 2.2 `==` 的语义（`CommonData.h:180`）

```cpp
return bcp_47 == another.bcp_47 && display_name == another.display_name && translator == another.translator;
```

**三字段全等才算同一语言**。这一点直接决定了"多 BCP-47 相同翻译文件并存"的行为。

## 3. 多翻译文件并存（参考 commit `0e1bdfc` "语言列表中允许加载多个BCP-47相同的翻译文件"）

### 3.1 改前

旧版 `GeneralSettingData` 只有 `WORD language`（language id），`GeneralSettingsDlg` 用 `language_info.language_id == m_data.language` 选当前条目。BCP-47 翻译并不同步：两个 `*.ini` 即使 `bcp_47` 相同，会因 `display_name`/`translator` 不同而被视为不同——但配置里只存 `WORD`，丢了 `display_name/translator` 信息，导致同 BCP-47 文件里只有一个能被选为当前语言。

### 3.2 改后（`CommonData.h:171`、`CommonData.cpp:296`）

- `WORD language` → `LanguageInfo language`（`CommonData.h:374` 处替换）；
- 持久化从整数改成 `bcp_47|display_name|translator` 字符串（`TrafficMonitor.cpp:62/297`）；
- `GeneralSettingsDlg` 比较改为 `language_info == m_data.language`（`GeneralSettingsDlg.cpp:358/512`）；
- `CStrTable::Init` 的"匹配则覆盖"分支用同一个 `LanguageInfo::operator==`（`StrTable.cpp:96`）。

**结果**：`m_language_list` 允许多条 `bcp_47` 相同但 `display_name/translator` 不同的条目并存（`StrTable.cpp:103-105` 的 `if (iter == m_language_list.end()) push_back`），用户在选项设置里能看到并选择"法语 (Toto 译)"和"法语 (Tata 译)"两条；只有其中与 `theApp.m_general_data.language` **三字段全等**的那条才会覆盖当前语言表（`StrTable.cpp:96-100`）。

> 注意：commit 同步把 `operator==` 从 `CStrTable::LanguageInfo` 内嵌类挪到了顶层 `LanguageInfo`（顶层化后两者即同一个），并把"匹配覆盖"语义显式化为 `language_info == theApp.m_general_data.language`（见 `StrTable.cpp:96` 改动）。

## 4. 语言列表的呈现与运行时切换

- 选项 → 常规 → "语言" ComboBox：`GeneralSettingsDlg.cpp`（具体行号在改前 355、改后同区域）；
- ComboBox 第 0 项是"跟随系统"，序号 1 起才是 `m_language_list` 中的真实语言（`GeneralSettingsDlg.cpp` `m_language_combo.SetCurSel(current_language_index + 1)`）；
- 用户确定后：`m_data.language` 写入 `IniHelper`，保存时由 `toConfigString` 序列化；
- 切换语言的关键 API：`CCommon::SetThreadLanguage(WORD language_id)`（`Common.cpp:1123`）—— 在 `InitInstance` 时（`TrafficMonitor.cpp:973`）、后台更新线程（`TrafficMonitor.cpp:609`）、网络信息对话框（`NetworkInfoDlg.cpp:161`）等地方调用，保证 MFC 资源按目标 LCID 加载。

## 5. 插件如何获取主程序字符串：`ITrafficMonitor::GetStringRes`

接口定义：`include/PluginInterface.h:422`

```cpp
virtual const wchar_t* GetStringRes(const wchar_t* key, const wchar_t* section) = 0;
```

主程序实现：`TrafficMonitor.cpp:1494`

```cpp
const wchar_t* CTrafficMonitorApp::GetStringRes(const wchar_t* key, const wstring& section)
{
    return m_str_table.LoadText(key, section).c_str();
}
```

- 委托给 `m_str_table.LoadText(key, section)`，所以插件只要知道 key + section，就能拿到**当前语言**下已经切换好的字符串，无需关心 ini 解析；
- section 典型值：`"text"` / `"menu"`，但任意 ini section 名都行；
- 接口由 `ITMPlugin::OnInitialize(ITrafficMonitor* pApp)`（`PluginInterface.h:323`）把 `pApp` 指针传给插件后，插件可保存并自由调用；
- 提交记录 `cd46521`："插件 ITrafficMonitor 接口新增获取字符串资源的接口" 即此扩展。

### 5.1 插件显示项的自定义字符串

插件通过 `IPluginItem` 提供自己的 `GetItemName / GetItemLableText`，主程序展示项层 `CommonDisplayItem::GetItemName`（`DisplayItem.cpp:56`）优先返回 `plugin_item->GetItemName()`；当插件想要"复用主程序翻译键"而不是自带多语言时，可以让 `GetItemName` 内部调 `pApp->GetStringRes(L"IDS_XXX", L"text")`。搜索结果显示当前主程序内尚未直接调 `pApp->GetStringRes`（`Grep` 在 `TrafficMonitor/` 项目内仅命中 `TrafficMonitor.cpp:1494` 的实现），典型用法在插件侧。

## 6. README / Help 双语文档

仓库根目录：

- `README.md` / `README_en-us.md`
- `Help.md` / `Help_en-us.md`

互链方式：
- `README.md:1` 开头 `**简体中文 | [English](./README_en-us.md)**`；
- `README_en-us.md:1` 反向链回 `**[简体中文](./README.md) | English**`；
- `Help.md:1` / `Help_en-us.md:1` 同模式。

内容上：除标题段落互译外，正文段落均与代码层脱钩——**不在仓库里维护翻译字符串**。所有可翻译字符串都在 `TrafficMonitor/language/*.ini` 与内置 `IDR_LANGUAGE` 资源里。

## 7. `language/` 目录与 BCP-47 标识

`TrafficMonitor/language/` 目录下当前有 15 个翻译文件：`English.ini`、`Simplified_Chinese.ini`、`Traditional_Chinese.ini`、`French.ini`、`German.ini`、`Hebrew.ini`、`Hungarian.ini`、`Italian.ini`、`Korean.ini`、`Polish.ini`、`Portuguese_Brazilian.ini`、`Russian.ini`、`Spanish.ini`、`Turkish.ini`、`Vietnamese.ini`。

每个文件结构（以 `Simplified_Chinese.ini:1` 为例）：

```ini
[general]
BCP_47 = "zh-CN"
DISPLAY_NAME = "简体中文"
TRANSLATOR = ""
TRANSLATOR_URL = ""
DEFAULT_FONT = "微软雅黑"

[text]
IDS_CHECK_UPDATE_FAILD = "检查更新失败，请检查你的网络连接！"
...
```

- 关键字段：`BCP_47` 唯一标识语言；`DISPLAY_NAME` 显示在选项菜单；`DEFAULT_FONT` 给本语言的默认字体；`TRANSLATOR` / `TRANSLATOR_URL` 给作者署名；
- `[text]` / `[menu]` 段就是主程序可见的所有字符串键值；
- 缺失键时主程序回退到 `IDR_LANGUAGE_DEFAULT` 资源，确保即便翻译未完成也不显示空白。

## 8. 关键关系一览

| 关系 | 说明 |
|------|------|
| `CStrTable::Init` 三步 | 枚举内置 → 读默认 → 扫 `language/`（`StrTable.cpp:64-105`） |
| `LoadText` 兜底链 | 插件文件覆盖 → `IDR_LANGUAGE` → `IDR_LANGUAGE_DEFAULT` |
| `language_info ==` | 三字段（bcp_47 + display_name + translator）全等才算同一项（`CommonData.h:180`） |
| 持久化字段 | `general/language` = `bcp_47\|display_name\|translator`（`CommonData.cpp:298`） |
| 线程语言切换 | `CCommon::SetThreadLanguage(language_id)`（`Common.cpp:1123`），`language_id = LocaleNameToLCID(bcp_47)` |
| 插件字符串 | `pApp->GetStringRes(key, section)` → `m_str_table.LoadText`（`TrafficMonitor.cpp:1494`） |
| BCP-47 并存 | 同 bcp_47 但 display_name/translator 不同的多个 ini 可并存于 `m_language_list`（`StrTable.cpp:103`） |
| README/Help | 仓库级 README/Help 双语互链；可翻译字符串不在这里，在 `language/*.ini` 与 `IDR_LANGUAGE` |