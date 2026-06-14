# Skin System（皮肤系统）

本文档覆盖 TrafficMonitor 主程序中"皮肤"的全部实现：磁盘布局、加载与解析、管理、主题色、自适配、主窗口如何使用皮肤，以及插件显示项与皮肤的共存。

所有结论来自源码，引用形如 `文件路径:行号`。

---

## 1. 皮肤目录与文件格式约定

皮肤资源位于 `skins/` 子目录下，每个皮肤是一个以皮肤名为目录名的文件夹（`TrafficMonitor.cpp:952`，`TrafficMonitor.h:43` `m_skin_path`）。

```
skins/
  0默认皮肤/
    skin.xml         # 新版皮肤描述文件（推荐）
    skin.ini         # 旧版皮肤描述文件（兼容用）
    background.png   # 小窗口背景（PNG 优先）
    background_l.png # 大窗口背景（PNG 优先）
    background.bmp   # 小窗口背景（PNG 失败时回退）
    background_l.bmp # 大窗口背景（PNG 失败时回退）
    background_mask.bmp / background_mask_l.bmp   # 旧版掩码（用于 SetWindowRgn）
  皮肤01/, 皮肤02/, ...
```

约定由 `SkinFile.cpp:90` 在 `Load(skin_name)` 中固定：先尝试 `skin.xml`，不存在则回退 `skin.ini`；同样的，背景图片先尝试 `.png` 再回退 `.bmp`（`SkinFile.cpp:131-153`）。

当前仓库自带的皮肤 `0默认皮肤/` 仅提供 `skin.ini` + 两张 png 背景（`skins/0默认皮肤/` 目录）。`xml_test/` 是测试目录。

## 2. 皮肤解析层

### 2.1 CSkinFile —— 皮肤的"模型对象"

定义：`SkinFile.h:7-159`。关键职责：

- `bool Load(const wstring& skin_name)` —— `SkinFile.cpp:88-155`，根据 `skin_name` 在 `theApp.m_skin_path + skin_name` 目录下找 `skin.xml` 或 `skin.ini`，完成解析 + 字体 + 背景加载。
- 暴露数据：
  - `GetSkinInfo()` 返回 `SkinInfo`（`SkinFile.h:22-39`）：`text_color[]`、`specify_each_item_color`、`skin_author`、`font_info`（`FontInfo`，定义在 `CommonData.h:115-145`）、`display_text`（`DispStrings`，定义在 `CommonData.h:60-79`）。
  - `GetLayoutInfo()` 返回 `LayoutInfo`（`SkinFile.h:76-82`）：包含 `text_height`、`no_label`、以及两套 `Layout`：`layout_l`（显示更多信息）和 `layout_s`（不显示更多信息）。每套 `Layout` 内部用 `std::map<CommonDisplayItem, LayoutItem>` 保存每一项的 `x/y/width/align/show`（`SkinFile.h:51-58`）。
  - `GetPreviewInfo()` 返回 `PreviewInfo`（`SkinFile.h:85-96`）：预览图大小 + 两个位置 `l_pos/s_pos`。
  - `GetBackgroundL()` / `GetBackgroundS()` 返回 `CImage`（bmp 路径），`IsPNG()` 标识是否 png 路径，`SetAlpha()` 设置不透明度（仅 PNG 有效）。
- `SetSettingData(const SkinSettingData&)` —— `SkinFile.cpp:401-411`：在皮肤已经载入之后注入用户在当前皮肤上的覆盖设置（字体、显示文本、颜色），会在字体变化时重建 `m_font`。
- `DrawInfo(CDC*, bool show_more_info)` —— `SkinFile.cpp:555-612`：把当前皮肤画到主窗口；PNG 走 GDI+ + `UpdateLayeredWindow` 走 alpha 通道，BMP 走 GDI + `CDrawDoubleBuffer`。
- `DrawPreview(CDC*, CRect)` —— `SkinFile.cpp:413-553`：画预览大图。
- `GetSkinDisplayItems()` —— `SkinFile.cpp:757-764`：返回该皮肤中"实际声明过布局"的所有 `CommonDisplayItem`（用于颜色/文本配对的索引）。
- `GetDisplayItemXmlNodeName()` —— `SkinFile.cpp:708-755`：把 `DisplayItem` 枚举映射到 XML 节点名（`up`、`down`、`cpu`、`gpu`、`cpu_temperature` 等）。

### 2.2 XML 解析：tinyxml2 + CTinyXml2Helper

皮肤 XML 使用 tinyxml2。`CTinyXml2Helper`（`TinyXml2Helper.h:6-27`，`TinyXml2Helper.cpp`）对 tinyxml2 做了三点薄封装：

- `LoadXmlFile(doc, file_path)`（`TinyXml2Helper.cpp:5-13`）—— 由于 `XMLDocument::LoadFile` 不支持 Unicode，这里手工读 UTF-8 字节再用 `XMLDocument::Parse`。
- `IterateChildNode(ele, fun)` —— `TinyXml2Helper.cpp:15-32`：手动遍历兄弟节点，避开 tinyxml2 自己 `FirstChildElement` 不递归同名兄弟的痛点。
- `ElementAttribute`、`ElementName`、`ElementText`、`StringToBool` —— 容错版本，找不到属性/文本时返回空串或 `false`，调用方无需判空。

`CSkinFile::LoadFromXml`（`SkinFile.cpp:157-300`）使用上述 helper 读取：

```
<skin>
  <text_color>255,255,255</text_color>
  <specify_each_item_color>0</specify_each_item_color>
  <skin_author>xxx</skin_author>
  <font name="..." size="9" style="0"/>
  <display_text>
    <up>上传:</up>
    <cpu>CPU:</cpu>
    ...
  </display_text>
</skin>
<layout text_height="20" no_label="0">
  <layout_l width="..." height="...">
    <up x=".." y=".." width=".." align=".." show="1"/>
    <down .../>
    <cpu .../>
    <memory .../>
    ...
  </layout_l>
  <layout_s ...>...</layout_s>
</layout>
<preview width=".." height="..">
  <l x=".." y=".."/>
  <s x=".." y=".."/>
</preview>
<plugin_map>
  <插件节点名>插件 item id</插件节点名>
  ...
</plugin_map>
```

注意：
- `text_color` 在 XML 中是逗号分隔的颜色值数组；如果个数少于当前"全部显示项（含插件项）"的长度，会自动用首元素补齐（`SkinFile.cpp:184-190`）。
- `font` 的 `style` 是位标志：第 0 位 bold，第 1 位 italic，第 2 位 underline，第 3 位 strike_out（`SkinFile.cpp:208-211`）。
- `plugin_map` 解决"XML 节点名 ≠ 插件内部 id"的问题：`m_plugin_map[node_name] = plugin_id`（`SkinFile.cpp:148`，`SkinFile.cpp:257-264`），下游在解析 layout 子节点时通过此映射反查（`SkinFile.cpp:50-61`）。

### 2.3 INI 兼容解析：CSettingsHelper::LoadFromIni

为兼容旧版皮肤，`CSkinFile::LoadFromIni`（`SkinFile.cpp:302-393`）用 `CSettingsHelper` 读取 `skin.ini` 中固定字段：

- `[skin]` 段：`text_color`、`specify_each_item_color`、`skin_author`，以及 `up_string/down_string/cpu_string/memory_string` 等显示文本。
- `[layout]` 段：`text_height`、`no_text`（即 `no_label`）、`preview_width/height`，以及两套布局的 `*_x_l/_y_l/_width_l/_align_l/show_*_l` 等键。

字段清单见 `SkinFile.cpp:344-392`，仅覆盖 4 个内置项目（TDI_UP/TDI_DOWN/TDI_CPU/TDI_MEMORY）；插件项不出现在 ini 皮肤中。

### 2.4 CSimpleXML —— 任务计划程序 XML 的读取

`SimpleXML.h`/`SimpleXML.cpp` 是一个独立的、纯字符串匹配的"简易 XML 读取器"，仅用于解析 Windows 任务计划程序返回的 XML 片段（`auto_start_helper.cpp:382-385`），与皮肤解析无关。它提供 `GetNode(name[, parent])`，按 `<name>...</name>` 抓取文本。

### 2.5 CIniHelper / CSettingsHelper

- `CIniHelper`（`IniHelper.h`、`IniHelper.cpp`）：底层 INI 读写，文件编码自动识别（BOM 判定 UTF-8），写入默认带 BOM，`Save()` 才落盘。
- `CSettingsHelper : public CIniHelper`（`SettingsHelper.h:5-41`，`SettingsHelper.cpp`）：包装"主窗口颜色 + 字体 + 显示文本"等高频读写对，给皮肤管理器、设置对话框、CSkinFile::LoadFromIni 复用。常用方法：
  - `LoadMainWndColors` / `SaveMainWndColors`（`SettingsHelper.cpp:39-71`）：把逗号分隔的颜色字符串拆成 `map<CommonDisplayItem, COLORREF>`。
  - `LoadFontData` / `SaveFontData`（`SettingsHelper.cpp:15-37`）：把 `FontInfo` 拆成 `font_name/font_size/font_style[4]`。
  - `LoadDisplayStr` / `SaveDisplayStr` 与 `LoadPluginDisplayStr` / `SavePluginDisplayStr`（`SettingsHelper.cpp:119-161`）：内置项与插件项分开存取，section 不同（详见下文 settings 文档）。

`CSettingsHelper` 默认无参构造函数会把 `m_file_path` 初始化为 `theApp.m_config_path`（`SettingsHelper.cpp:5-8`）——这一点对"皮肤管理器写全局 skin 配置"很重要。

## 3. CSkinManager —— 皮肤的全局管理器

定义：`SkinManager.h:5-35`，实现：`SkinManager.cpp`。作为单例（`SkinManager.cpp:7` `static CSkinManager m_instance;`）。

### 3.1 加载与枚举

`Init()`（`SkinManager.cpp:11-58`）只跑一次（`m_init` 守卫）：

1. 用 `CCommon::GetFiles(theApp.m_skin_path + L"*", ...)` 列出所有子目录名作为皮肤名保存到 `m_skins`（`SkinManager.cpp:16-20`）。若为空则放一个空字符串占位（`SkinManager.cpp:21-22`）。
2. 读取 INI 中所有以 `skin_` 为前缀的 section（`SkinManager.cpp:26`，常量在 `SkinManager.cpp:9`），对每个名字：
   - 用 `std::find` 检查该皮肤是否仍存在（已被删除的旧皮肤会被忽略，`SkinManager.cpp:30-32`）。
   - 临时 `CSkinFile skin_file; skin_file.Load(skin_name);` 取到 `skin_all_items`，再调用 `LoadMainWndColors` / `GetBool` / `LoadFontData` / `LoadDisplayStr` / `LoadPluginDisplayStr` 填充 `SkinSettingData` 并放进 `m_skin_setting_data_map[skin_name]`（`SkinManager.cpp:33-54`）。

`GetSkinName(i)` / `FindSkinIndex(name)` / `Size()` / `GetSkinNames()` 仅做简单的索引/反查（`SkinManager.cpp:65-108`）。

### 3.2 规范化名字：SkinNameNormalize

`SkinNameNormalize`（`SkinManager.cpp:194-202`）只去掉 `skin_name` 开头的 `'/'` 或 `'\\'`，用于消除历史版本的"路径前缀"残留。`LoadConfig` 中也用同样手法兼容 `".\\skins\\"` 前缀（`TrafficMonitor.cpp:108-110`）。

### 3.3 单皮肤覆盖数据：SkinSettingData 与 m_skin_setting_data_map

`SkinSettingData`（`CommonData.h:232-241`）保存用户在某个皮肤上的覆盖项：`font`、`disp_str`、每项 `text_colors`、`specify_each_item_color`。

- 注入默认值：`SkinSettingDataFronSkin(skin_data, skin_file)`（`SkinManager.cpp:149-192`）：从已经 Load 的 `CSkinFile` 中读取作者/字体/颜色/默认文本，作为缺省值。
- 用户在 Options 对话框改完皮肤文字/字体/颜色后，`TrafficMonitorDlg.cpp:858-861` 调用 `CSkinManager::Instance().AddSkinSettingData(skin_name, skin_data)` 与 `Save()`，把覆盖数据落到 INI 的 `skin_<皮肤名>` section 中。
- `Save()`（`SkinManager.cpp:115-147`）：写所有皮肤的覆盖项，并 `RemoveSection` 那些在 `m_skins` 中已经不存在的旧 section。

### 3.4 Save 与删除旧 section

`Save()` 末尾遍历 `GetAllAppName(SKIN_SETTINGS_PREFIX)`，对在 `m_skin_setting_data_map` 找不到的皮肤配置调用 `RemoveSection`（`SkinManager.cpp:132-144`）——"换皮肤/删皮肤"时清理 INI。

## 4. 自适配：暗色/亮色双皮肤

### 4.1 数据结构与开关

相关字段全部在 `MainConfigData`（`CommonData.h:190-221`）：

- `bool skin_auto_adapt`（`CommonData.h:207`）：是否启用深浅色自适配。
- `wstring skin_name_light_mode` / `skin_name_dark_mode`（`CommonData.h:208-209`）：两个模式下要切换到的皮肤名。

INI 落点：`[skins]` section，键 `skin_auto_adapt`、`skin_name_dark_mode`、`skin_name_light_mode`（`TrafficMonitor.cpp:112-114` 读，`TrafficMonitor.cpp:324-326` 写）。

### 4.2 触发点

主对话框定时器中：判断当前是否 Windows10+ 并且 `skin_auto_adapt=true`，用 `FindSkinIndex` 查目标皮肤索引，再调 `CTrafficMonitorDlg::ApplySkin(skin_index)`（`TrafficMonitorDlg.cpp:1866-1870`）。`ApplySkin`（`TrafficMonitorDlg.cpp:973-1000`）会重新 Load 布局、读取用户在该皮肤的覆盖项、`SetItemPosition`、`SetTransparency`、`Invalidate` 并 `SaveConfig`。

### 4.3 用户设置入口：CSkinAutoAdaptSettingDlg

`SkinAutoAdaptSettingDlg.cpp:13-83`：纯对话框，两个 `CComboBox` 分别对应深色/亮色皮肤，从 `CSkinManager::Instance().GetSkinNames()` 拉取候选项，初始值用 `FindSkinIndex(cfg_data.skin_name_dark_mode/_light_mode)` 定位（`SkinAutoAdaptSettingDlg.cpp:67-70`），回写也写回 `cfg_data.skin_name_*_mode`（`SkinDlg.cpp:194-201`）。

入口在 `SkinDlg.cpp` 的"皮肤自适配"按钮：`OnBnClickedSkinAutoAdaptButton`（`SkinDlg.cpp:189-202`）。"自动切换"复选框本身在 `CSkinDlg::OnOK` 里写回 `cfg_data.skin_auto_adapt`（`SkinDlg.cpp:207`），并在 `OnInitDialog` 里根据它启用/禁用按钮（`SkinDlg.cpp:143-144`）。

## 5. 主题色：m_theme_color = CCommon::GetWindowsThemeColor()

### 5.1 获取

`CCommon::GetWindowsThemeColor()`（`Common.cpp:1328-1343`）调用 `DwmGetColorizationColor` 取当前 DWM 主题色，返回 `COLORREF`。

`theApp.m_theme_color`（`TrafficMonitor.h:188`）在 `CTrafficMonitorApp` 构造时取一次（`TrafficMonitor.cpp:56`），并通过 `GetThemeColor()/SetThemeColor()` 暴露给 `ITrafficMonitor`（`TrafficMonitor.cpp:1319-1327`）。

### 5.2 下游消费

- `TaskBarSettingData::GetUsageGraphColor()`（`CommonData.cpp:267-285`）：当 `graph_color_following_system` 开启时，使用 `theApp.GetThemeColor()` 并通过 `CDrawingManager::RGBtoHSL` 转 HSL，再按 `m_last_light_mode` 把亮度固定为 0.7（浅色）或 0.4（深色）。
- 通知区图标的"自动适配"使用 Windows10 的浅色/深色判定而不是主题色：见 `CTrafficMonitorApp::AutoSelectNotifyIcon`（`TrafficMonitor.cpp:886-906`），根据 `IsWindows10LightTheme()` 在白色/黑色图标之间切换。
- `CTrafficMonitorDlg::OnDwmCompositionChanged`/`OnColorizationColorChanged`（路径见 `TrafficMonitorDlg.cpp:3072-3073`）会把新的主题色 `SetThemeColor` 回去。

## 6. 主窗口怎么用皮肤

### 6.1 数据成员

`CTrafficMonitorDlg::m_skin` 是 `CSkinFile` 实例（`TrafficMonitorDlg.h`），`m_skin_selected` 是当前皮肤在 `CSkinManager` 中的索引（`TrafficMonitorDlg.cpp:1061`）。

### 6.2 初始化与 Apply 流程

- `OnInitDialog`（`TrafficMonitorDlg.cpp:1060-1071`）：
  1. `CSkinManager::Instance().Init()` —— 枚举 `skins/` 全部皮肤、加载所有 `skin_*` 配置。
  2. `m_skin_selected = FindSkinIndex(m_cfg_data.m_skin_name)`。
  3. `LoadSkinLayout()`（`TrafficMonitorDlg.cpp:879-885`）调用 `m_skin.Load(skin_name)`；若皮肤 `no_label=true` 强制关闭 `swap_up_down`。
  4. 用 `SkinSettingDataFronSkin` 取默认 → `GetSkinSettingDataByIndex` 取用户覆盖 → `m_main_wnd_data.FormSkinSettingData(...)` 写入 `MainWndSettingData`。
- `ApplySkin(skin_index)`（`TrafficMonitorDlg.cpp:973-1000`）：换皮的标准流程，详见上文 4.2。
- `SetTransparency()`（`TrafficMonitorDlg.cpp:228-238` 等）：用 `m_cfg_data.m_transparency` 调 `m_skin.SetAlpha(transparency * 255 / 100)`，仅 PNG 有效（`SkinFile.cpp:396-399`）。

### 6.3 实际绘制

主窗口绘制入口在 `CTrafficMonitorDlg::OnPaint` → `m_skin.DrawInfo(&dc, m_cfg_data.m_show_more_info)`（`TrafficMonitorDlg.cpp:2773`），`DrawInfo`（`SkinFile.cpp:555-612`）流程：

- 选 layout（l/s）；
- PNG：`CreateCompatibleDC` + `CreateCompatibleBitmap` → GDI+ 绘背景 → `DrawItemsInfo` → `DrawCommonHelper::GetBitmapAlphaPixel` + `FixBitmapTextAlpha` → `UpdateLayeredWindow(m_hWnd, ..., ULW_ALPHA)`；
- BMP：`CDrawDoubleBuffer` + `CDrawCommon` 直接绘背景与文本。
- 窗口尺寸用 `SetItemPosition`（`TrafficMonitorDlg.cpp:867-877`）从 `m_skin.GetLayoutInfo().layout_l.width/height` 与 `layout_s.width/height` 中选。

文本颜色与显示文本来源：`DrawItemsInfo`（`SkinFile.cpp:615-705`）从 `theApp.m_main_wnd_data.text_colors/specify_each_item_color` 取颜色，从 `theApp.m_main_wnd_data.disp_str` 取标签文本，从 `theApp` 当前缓存的 `m_in_speed / m_cpu_usage` 等取数值文本（通过 `CommonDisplayItem::GetItemValueText`）。

### 6.4 旧版掩码图：SetWindowRgn

`LoadBackGroundImage()`（`TrafficMonitorDlg.cpp:887-928`）读 `BACKGROUND_MASK_L/S`（定义在 `CommonData.h` 附近的常量），从掩码生成 `CRgn`，调 `SetWindowRgn(wnd_rgn, TRUE)` 用于不规则窗口形状。

## 7. 用户在 UI 里切换皮肤的入口

- `CSkinDlg`（`SkinDlg.h/cpp`，资源 `IDD_SKIN_DIALOG`）：挂在主菜单的"更换皮肤"上（在 `OnInitDialog` 中填充 `m_skin_list_box`，`SkinDlg.cpp:122-125`）。选中项通过 `ShowPreview` 调 `m_skin_data.Load(...)`（`SkinDlg.cpp:58-88`）实时预览；`OnOK` 写回 `cfg_data.skin_auto_adapt`，并通过"自适配设置"按钮弹出 `CSkinAutoAdaptSettingDlg`。
- `ApplySkin` 是主对话框上实际换皮的入口，被多处调用（自动适配、菜单命令、对话框 OK）。
- 没有专门叫 `SkinManagerDlg` 的对话框；`CSkinManager` 通过 `Instance()` 单例对外提供列表/索引接口。

## 8. 与插件显示项共存

### 8.1 m_all_display_items_with_plugins

定义在 `CPluginManager`（`PluginManager.cpp:213-216`），由 `LoadPlugins`（`PluginManager.cpp:127-134`）填充为"内置 `AllDisplayItems` ∪ 已加载插件的 `IPluginItem*`"。它是任何需要枚举"现在共有多少个显示项"时的统一真相来源。

### 8.2 皮肤里有但插件未提供

皮肤 XML 的 `<layout>` 里出现的项目，若名字既不是内置项（`AllDisplayItems`）也无法在 `plugin_map` 找到对应 `IPluginItem`（即"插件没装"），则该节点不会被加入 `Layout::layout_items`（`SkinFile.cpp:39-62`）：循环中先尝试匹配 `AllDisplayItems`，匹配失败再用 `m_plugin_map[node]` 反查 `theApp.m_plugins.GetPluginItems()`，找不到就不入 map。结果是：

- `DrawItemsInfo` 在遍历 `map<DisplayItem, DrawStr>` 时只会画"插件装了且皮肤声明了"的项目（`SkinFile.cpp:618-624`）。
- `DrawItemsInfo` 单独遍历 `theApp.m_plugins.GetPluginItems()`（`SkinFile.cpp:663-704`），若皮肤没声明该插件项的位置（`layout.GetItem(plugin_item).show` 为 false）也不会画——插件项的最终显示位置完全由皮肤决定。
- `LoadFromXml` 的颜色数组补齐（`SkinFile.cpp:184-190`）也以 `AllDisplayItemsWithPlugins().size()` 为目标长度，避免"插件已加载但皮肤不知道有几项"导致颜色越界。

### 8.3 插件项自绘

`SkinFile.cpp:680-691` 与 `SkinFile.cpp:523-532`：若插件项 `IsCustomDraw()`，通过 `plugin_item->DrawItem(hdc, x, y, w, h, brightness>=128)` 走插件自己画（典型场景：图表/进度条）；同时通过 `OnExtenedInfo(EI_DRAW_TASKBAR_WND, ...)` 与 `EI_VALUE_TEXT_COLOR` 把上下文告知插件（API v2+）。

## 9. 关键文件清单（再次汇总）

- `TrafficMonitor/SkinFile.h/.cpp` —— 皮肤模型 + XML/INI 解析 + 绘制。
- `TrafficMonitor/SkinManager.h/.cpp` —— 单例管理器 + `m_skin_setting_data_map` + 自适配名空间。
- `TrafficMonitor/SkinDlg.h/.cpp` —— 用户选皮肤的对话框。
- `TrafficMonitor/SkinAutoAdaptSettingDlg.h/.cpp` —— 深/浅色皮肤名选择。
- `TrafficMonitor/CAutoAdaptSettingsDlg.h/.cpp` —— 任务栏窗口的颜色预设自适配（与皮肤自适配并列的另一组开关）。
- `TrafficMonitor/TinyXml2Helper.h/.cpp` —— tinyxml2 容错封装。
- `TrafficMonitor/SettingsHelper.h/.cpp` —— INI 高层包装（皮肤覆盖项的存读）。
- `TrafficMonitor/CommonData.h` —— `FontInfo`、`DispStrings`、`SkinSettingData`、`MainWndSettingData`（含 `ToSkinSettingData/FormSkinSettingData`）、`MainConfigData`（含 `skin_auto_adapt/skin_name_dark_mode/skin_name_light_mode`）。
- `TrafficMonitor/TrafficMonitor.cpp` —— 启动路径装配、`m_skin_path/m_config_path`、主题色、`LoadConfig/SaveConfig`、`LoadGlobalConfig/SaveGlobalConfig`。
- `TrafficMonitor/TrafficMonitor.h` —— 全局路径与数据成员。
- `TrafficMonitor/TrafficMonitorDlg.cpp` —— 主窗口用皮肤的入口（`ApplySkin`、`LoadSkinLayout`、`DrawInfo` 调用）。
- `TrafficMonitor/PluginManager.cpp` —— `m_all_display_items_with_plugins`。
- `TrafficMonitor/Common.cpp` —— `GetWindowsThemeColor`。