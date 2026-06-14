# 绘制抽象（IDrawCommon / DrawCommonFactory / 后端支持）主题文档

## 1. 总体结构

```
IDrawCommon / IDrawBuffer                                       (IDrawCommon.h:10/4)
  ├── CDrawCommon                                               (DrawCommon.h:8,  GDI 实现)
  ├── CTaskBarDlgDrawCommon / CTaskBarDlgDrawBuffer[UseDComp]   (TaskBarDlgDrawCommon.h:758/847/867,  D2D1 实现)
  └── CDrawDoubleBuffer                                         (DrawCommon.h:65,  GDI 双缓冲)
CDrawCommonEx (GDI+)                                            (DrawCommonEx.h:6)  // 当前只用于 SkinFile 绘制主窗口

AlignedUnionStorage<CDrawCommon, CTaskBarDlgDrawCommon>          (DrawCommonFactory.h:27,  栈内联合存储)
AllInvolvedDrawCommonObjectsStorage                              (DrawCommonFactory.h:43,  加独占指针收尾)
GetInterfaceFromAllInvolvedDrawCommonObjects                    (DrawCommonFactory.cpp:4,  按 RenderType 在栈上 placement-new)
```

后端支撑（D2D1/D3D10/DComp/DXGI/WIC）由 `TaskBarDlgDrawCommon.h`、`D2D1Support.h`、`D3D10Support1.h`、`DCompositionSupport.h`、`Dxgi1Support2.h`、`WIC.h` 提供。

## 2. IDrawCommon 接口

`IDrawCommon`（`IDrawCommon.h:10`）是一个抽象绘制接口，所有绘制后端都必须实现它。关键成员：

| 成员 | 用途 |
|------|------|
| `enum class StretchMode { STRETCH, FILL, FIT }` | 位图拉伸模式（`IDrawCommon.h:14-19`） |
| `enum class Alignment { LEFT, RIGHT, CENTER }` | 文本对齐（`IDrawCommon.h:22-27`） |
| `SetBackColor(COLORREF, BYTE alpha = 255)` | 设置背景色 |
| `SetFont(CFont*)` | 绑定字体（`CFont*` 由调用方负责生命周期） |
| `DrawWindowText(rect, str, color, align, draw_back_ground, multi_line, alpha)` | 在矩形内绘制文字 |
| `SetDrawRect(rect)` | 设定/改变裁剪区 |
| `FillRect(rect, color, alpha)` | 纯色填充 |
| `DrawRectOutLine(rect, color, width, dot_line, alpha)` | 矩形边框，`dot_line=true` 时为虚线 |
| `DrawLine(start_point, height, color, alpha)` | 竖直向上 `height` 像素的线段（从 `start_point` 出发） |
| `SetTextColor(COLORREF, BYTE alpha)` | 文字颜色 |
| `DrawBitmap(HBITMAP, start_point, size, stretch_mode, alpha)` | 位图绘制 |
| `GetDC()` / `GetTextWidth()` | 仅 GDI/GDI+ 实现中返回非空/非零，D2D 路径默认返回 null/0 |

`IDrawBuffer`（`IDrawCommon.h:4`）是一个空接口，作为 `CDrawDoubleBuffer` / `CTaskBarDlgDrawBuffer` / `CTaskBarDlgDrawBufferUseDComposition` 的基类标记。

## 3. DrawCommonFactory 的"按可用 API 创建"策略

`DrawCommonFactory.h` 提供一套手工"placement-new + 独占指针销毁"机制，规避堆分配：

- `AlignedUnionStorage<Ts...>`（`IDrawCommon.h:79`）：用 `alignas` + 大小为 `max(sizeof(Ts)...)` 的字节数组作为栈上联合存储。
- `DrawCommonUnionStorage = AlignedUnionStorage<CDrawCommon, CTaskBarDlgDrawCommon>`（`DrawCommonFactory.h:27`）：容纳所有可能的 DrawCommon 实现。
- `DrawBufferUnionStorage = AlignedUnionStorage<CDrawDoubleBuffer, CTaskBarDlgDrawBuffer, CTaskBarDlgDrawBufferUseDComposition>`（`DrawCommonFactory.h:32`）：容纳所有可能的 Buffer。
- `StackObjectDeleter`（`DrawCommonFactory.h:9`）：通过 `Destroy` 调用 `~T()` 而不释放内存，配合栈内存使用。
- `AllInvolvedDrawCommonObjectsStorage`（`DrawCommonFactory.h:43`）：把联合存储 + 独占指针打包，析构时先 `m_unique_draw_common` 再 `m_unique_draw_buffer`（依声明逆序）。

`GetInterfaceFromAllInvolvedDrawCommonObjects`（`DrawCommonFactory.cpp:4`）的流程：
1. 把 `AllInvolvedDrawCommonObjectsStorage` reinterpret 成 `InvolvedDrawCommonStorages`，分别取到 `p_draw_buffer` 与 `p_draw_common` 裸指针。
2. 遍历 `initializer_list`（每项形如 `{RenderType, lambda(IDrawBuffer*, IDrawCommon*)}`），找到匹配 `render_type` 的那一项后 `placement-new` 构造对象（用具体子类 `static_cast` 还原），然后把裸指针包成 `UniqueIDrawBuffer`/`UniqueIDrawCommon` 接管生命周期。
3. 返回 `{p_draw_buffer, p_draw_common}` 的非拥有裸指针给调用方使用；对象随 `AllInvolvedDrawCommonObjectsStorage` 在栈帧结束时析构。

`CTaskBarDlg::ShowInfo`（`TaskBarDlg.cpp:99-185`）就是典型的工厂调用方：根据 `m_supported_render_enums.GetAutoFitEnum()` 选 RenderType，再用三个 lambda 分别初始化 DEFAULT（`CDrawDoubleBuffer` + `CDrawCommon`，`TaskBarDlg.cpp:104-119`）、D2D1_WITH_DCOMPOSITION（`CTaskBarDlgDrawBufferUseDComposition` + `CTaskBarDlgDrawCommon`，`TaskBarDlg.cpp:120-147`）、D2D1（`CTaskBarDlgDrawBuffer` + `CTaskBarDlgDrawCommon`，`TaskBarDlg.cpp:148-184`）。

## 4. 后端与支持类一览

### 4.1 GDI（CDrawCommon / CDrawDoubleBuffer）

- `CDrawCommon`（`DrawCommon.h:8`）封装 MFC `CDC*`；`DrawWindowText` 走 `DrawCommonHelper::ProccessTextFormat` + `TextOut`/`DrawText`（`DrawCommon.cpp`）；`DrawBitmap` 走 `::BitBlt`/`::AlphaBlend`/`::TransparentBlt`；`FillRect` 直接 `::FillRect`。
- `CDrawDoubleBuffer`（`DrawCommon.h:65`）在构造函数里 `CreateCompatibleDC` + `CreateCompatibleBitmap`，析构时 `BitBlt` 回主 DC 并释放资源，是 GDI 路径唯一的"缓冲"。

### 4.2 GDI+（CDrawCommonEx）

`CDrawCommonEx`（`DrawCommonEx.h:6`）是当前项目里**仅由主窗口皮肤绘制使用**的 GDI+ 实现（被 `CSkinFile::DrawItemsInfo` 引用）。实现中：
- 文本：`m_pGraphics->DrawString` + `Gdiplus::StringFormat`（`DrawCommonEx.cpp:46-78`）。
- `DrawRectOutLine`/`DrawLine`/`DrawBitmap` 三个方法保留为**空实现**（`DrawCommonEx.cpp:92/96/118`），所以它并不能完全替代 `CDrawCommon`——只用于主窗口的皮肤绘制路径。
- `CGdiPlusHelper`（`DrawCommonEx.h:39`）做 `COLORREF` ↔ `Gdiplus::Color` 与 `CRect` ↔ `Gdiplus::RectF` 转换。

### 4.3 D2D1（CTaskBarDlgDrawCommon / D2D1Support / DWriteSupport）

- `D2D1Support.h` 定义了：
  - `CD2D1Support::CheckSupport` / `GetFactory`（D2D v1 factory）。
  - `CD2D1Support1::CheckSupport` / `GetFactory1`（D2D v1.1 factory，`CTaskBarDlgDrawCommonSupport` 初始化用）。
  - `CD2D1Device`（`D2D1Support.h:30`）：用 `CTrackableDevice` 模式持有 `ComPtr<ID2D1Device>`，并 `Recreate(Microsoft::WRL::ComPtr<IDXGIDevice>)`。
  - `CDWriteSupport::CheckSupport` / `GetFactory`（DirectWrite factory）。
- `CTaskBarDlgDrawCommonSupport`（`TaskBarDlgDrawCommon.h:126`）：拥有 `CD3D10Device1 m_d3d10_device1`、`CD2D1Device m_d2d1_device`、`CDCompositionDevice m_dcomposition_device`、`ComPtr<ID2D1StrokeStyle> m_p_ps_dot_like_style`（虚线样式，`DASH_STYLE_DASH` + `LINE_JOIN_MITER`）。构造时创建 stroke style，并以 `D3D10_CREATE_DEVICE_BGRA_SUPPORT`（`Debug` 下加 `D3D10_CREATE_DEVICE_DEBUG`）调 `m_d3d10_device1.SetFlags` 再 `RecreateAll()`（`TaskBarDlgDrawCommon.cpp:217-241`）。
- `CTaskBarDlgDrawCommon`（`TaskBarDlgDrawCommon.h:758`）：实现 `IDrawCommon`，但内部走的是 D2D 设备上下文（不是 `ID2D1HwndRenderTarget`）。构造时不分配，调用 `Create(support, d2d_context_support, size)`（`TaskBarDlgDrawCommon.cpp:1596`）时：
  1. `m_p_window_support->Resize(size)`。
  2. `m_p_d2d1_device_context_support->Resize(size)` 拿到 `ID2D1DeviceContext*`。
  3. `BeginDraw` + `SetTransform(Identity)` + `Clear(transparent_black)`。
- 析构（`TaskBarDlgDrawCommon.cpp:1673`）是这套 D2D 路径里最关键的一段：
  1. 走 `CGdiInteropObject` 时把 GDI 互操作纹理里的 GDI 绘制结果回灌 GPU 纹理（`SetGdiInteropTexture` + `DrawAlphaValueReduceEffect`，`ps_4_1` shader 把 alpha≈0/1 的像素规整化），再 `CreateSharedBitmap` + `DrawBitmap` 把 GPU 合成结果画到 D2D 设备上下文。
  2. 始终 `EndDraw`（保证 `Begin/End` 配对）。
- 文本绘制（`CTaskBarDlgDrawCommon::DrawWindowText`，`TaskBarDlgDrawCommon.cpp:1740`）：走 `IDWriteFactory::CreateTextLayout` + `ID2D1DeviceContext::DrawTextLayout`，支持 `D2D1_DRAW_TEXT_OPTIONS_NO_SNAP | D2D1_DRAW_TEXT_OPTIONS_CLIP`；当 `m_taskbar_data.enable_colorful_emoji` 时追加 `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`（`TaskBarDlgDrawCommon.cpp:1805`）。
- 位图绘制（`CTaskBarDlgDrawCommon::DrawBitmap`，`TaskBarDlgDrawCommon.cpp:1879`）通过 `m_p_d2d1_device_context_support->GetCachedBitmap(hbitmap)` 走 `CD2D1BitmapCache`。
- 矩形填充/边框/直线通过 `FillRectangle`/`DrawRectangle`/`DrawLine` + `ID2D1SolidColorBrush` 完成。

#### `ExecuteGdiOperation`（CTaskBarDlgDrawCommon::ExecuteGdiOperation）

`TaskBarDlgDrawCommon.h:811` 提供：
```cpp
template <class GdiOp>
void ExecuteGdiOperation(CRect rect, GdiOp gdi_op)
{
    auto& ref_gdi_interop_object = m_gdi_interop_object.Get();  // CGdiInteropObject
    auto old_hfont = ref_gdi_interop_object.m_gdi_interop_cdc.SelectObject(m_p_window_support->GetFont());
    ref_gdi_interop_object.m_gdi_interop_cdc.SetTextColor(m_text_color);
    TaskBarDlgUser32DrawTextHook::Details::DrawTextReplacedFunctionState state{...};
    {
        auto enable_guard = TaskBarDlgUser32DrawTextHook::EnableAllReplaceFunction(state);
        gdi_op(ref_gdi_interop_object.m_gdi_interop_cdc);  // 在 GDI 互操作 HDC 上跑 GDI 绘制
    }
    ref_gdi_interop_object.m_gdi_interop_cdc.SelectObject(old_hfont);
}
```
这段把"插件自绘要求原生 HDC"的情形桥接到 D2D：插件拿到的是一块共享 GDI 互操作 DC；析构时由 `CGdiInteropObject` 把这块 DC 拷贝回 GPU（`CTaskBarDlgDrawCommon::~CTaskBarDlgDrawCommon`）。`EnableAllReplaceFunction` 期间临时把 `User32!DrawTextA/W/ExA/ExW` 替换为 `ReplacedDrawTextCommon`（`TaskBarDlgDrawCommon.cpp:1177-1360`），让走 `DrawText` 的 GDI 调用在 ARGB32 DIB 上输出，再以 `AlphaBlend` 灌回原 DC，从而在 `WS_EX_LAYERED` 下仍能保留 alpha。

### 4.4 D3D10.1（CD3D10Device1 / D3D10Support1 / Image2DEffect）

- `D3D10Support1.h:18` 的 `CD3D10Device1` 通过 `CTrackableDevice` 持有 `ComPtr<ID3D10Device1>`、`Data` 内部结构记录 adapter/driver/flags/feature level/sdk version；`SetFlags(...)`/`SetDriverType(...)` 等链式 setter；`Recreate(p_adapter1)` 重建（`D3D10Support1.cpp`）。
- `CD3D10Support1::CheckSupport` / `GetDeviceList(bool force_refresh)`：返回 `vector<ComPtr<IDXGIAdapter1>>`（`D3D10Support1.h:82`）。
- `CShader`（`D3D10Support1.h:104`）封装 HLSL 编译（`D3DCompile` 来自 `d3dcompiler_47.dll`，由 `CDllFunctions::D3DCompile` 加载，`DllFunctions.cpp:34`）。
- `CD3D10DrawCallWaiter`（`D3D10Support1.h:168`）用 `ID3D10Query` 等 GPU 同步，1.5s 超时返回 `S_FALSE`。
- `CImage2DEffect`（`Image2DEffect.h`）是 GPU effect 的封装；在 `CTaskBarDlgDrawCommonWindowSupport::CD3DGdiInteropHelper` 里有具体使用（设置输入/输出纹理、绘制）。

### 4.5 DXGI 1.2（CDxgi1Support2 / CDxgiSwapChain1）

- `Dxgi1Support2.h:39` `CDxgiSwapChain1` 用 `Recreate(Microsoft::WRL::ComPtr<IUnknown>, DXGI_SWAP_CHAIN_DESC1, IDXGIOutput*)` 构造交换链，`Resize` 改尺寸；用 `CDxgiSwapChainResource<SwapChain>` 在 `Resize` 前后通知子类。
- `CDxgi1Support2::CheckSupport` / `GetFactory2` 提供 `IDXGIFactory2`（`CreateDXGIFactory2` 通过 `CDllFunctions::CreateDXGIFactory2` 拿，`DllFunctions.cpp:41`）。
- 实际交换链在 `CD2D1DeviceContextWindowSupport::CDCompositionHelper::InitializeSwapChain`（`TaskBarDlgDrawCommon.cpp:759`）里以 `DXGI_SWAP_CHAIN_DESC1` + `SwapEffect = FLIP_SEQUENTIAL` + `AlphaMode = PREMULTIPLIED` 创建并绑定到 `IDCompositionVisual`。

### 4.6 DComposition（CDCompositionDevice / CDCompositionSupport）

- `DCompositionSupport.h:12` 的 `CDCompositionDevice` 用 `CTrackableDevice` 持 `ComPtr<IDCompositionDevice>`；`Recreate(Microsoft::WRL::ComPtr<IDXGIDevice>)` 重建。
- `CDCompositionSupport::CheckSupport` 通过 `CDllFunctions::DCompositionCreateDevice`（`dcomp.dll`，`DllFunctions.cpp:37`）试探。
- `CD2D1DeviceContextWindowSupport::CDCompositionHelper`（`TaskBarDlgDrawCommon.h:320`）实现 `CDeviceResource<CDCompositionDevice> + CDeviceResource<CD3D10Device1>`：负责 `IDCompositionTarget`/`IDCompositionVisual`、交换链 `InitializeSwapChain`/`ResizeBuffers`/`Present`、DComp `Commit`。
- `CDCompositionHelper::Present`（`TaskBarDlgDrawCommon.cpp:873`）先 `IDXGISwapChain1::Present(1, 0)` 再 `m_p_composition_device->Commit()`。

### 4.7 WIC（CWICFactory / CD2D1BitmapCache）

- `WIC.h:7` 的 `CWICFactory` 单例封装 `IWICImagingFactory`，提供 `GetWIC` 静态访问。
- `CD2D1BitmapCache`（`TaskBarDlgDrawCommon.h:669`）：以 `ComPtr<ID2D1RenderTarget>` 初始化，把 `HBITMAP` 缓存为 `ComPtr<ID2D1Bitmap>`。`CreateD2D1BitmapFromHBitmap`（`TaskBarDlgDrawCommon.cpp:1501`）走 `WICBitmapUsePremultipliedAlpha`：先 `CWICFactory::GetWIC()->CreateBitmapFromHBITMAP`，再 `p_render_target->CreateBitmapFromWicBitmap`。
- 后台 GC：构造时启动 `std::thread`（`TaskBarDlgDrawCommon.h:704-723`）每 `gc_interval`（默认 60s）跑 `GCImpl`，清理过期的 `Cache`。
- `m_sp_data` 用 `shared_ptr` 持有；线程闭包捕获 `weak_ptr`，缓存全部释放后线程退出。

### 4.8 设备-资源跟踪

`RenderAPISupport.h` 提供跨文件的设备-资源跟踪模板：
- `CTrackableDevice<Device>`（`RenderAPISupport.h:72`）：`Storage` 持有 `Device*`；`shared_ptr<Storage>` 在设备/资源间共享。
- `CResourceTracker<Resource>`（`RenderAPISupport.h:138`）：保存所有 `CDeviceResource<Device>` 子类指针的 `unordered_set`。
- `CDeviceResourceBase<Device>`（`RenderAPISupport.h:219`）：CRTP 友好的资源基类，构造/拷贝/移动时调用 `BeginTrack/EndTrack` 加入/移出 `ResourcePointerSet`；析构受保护（仅 `CDeviceResource<Device>` 派生）。
- `CDeviceResource<Device>`（`RenderAPISupport.h:290`）：在 `CDeviceResourceBase` 之上加 `virtual void OnDeviceRecreate(DeviceType new_device) noexcept = 0`，设备重建时由 `NotifyAllResourceWhenDeviceRecreate`（`RenderAPISupport.h:214`）逐个调用。
- 具体子类：`CTaskBarDlgDrawCommonWindowSupport::CD3DGdiInteropHelper`（`TaskBarDlgDrawCommon.h:182`）继承 `CDeviceResource<CD3D10Device1>`；`CD2D1DeviceContextWindowSupport::CD2D1DeviceContextHelper` 继承 `CDeviceResource<CD2D1Device>`（`TaskBarDlgDrawCommon.h:284`）；`CD2D1DeviceContextWindowSupport::CDCompositionHelper` 多继承 `CDeviceResource<CDCompositionDevice> + CDeviceResource<CD3D10Device1>`（`TaskBarDlgDrawCommon.h:320-323`）。

## 5. 后端选优策略

### 5.1 CSupportedRenderEnums

`SupportedRenderEnums.h:5` 用 `std::bitset<3>` 表示三种可选渲染后端：
- `DEFAULT_INDEX = 0`（GDI 路径，恒为 1，`CSupportedRenderEnums` 默认构造即 `m_enums{1}`）。
- `D2D1_WITH_DCOMPOSITION_INDEX = 1`：`CDCompositionSupport::CheckSupport() && CDxgi1Support2::CheckSupport()`（`SupportedRenderEnums.cpp:7-9`）。
- `D2D1_INDEX = 2`：`CTaskBarDlgDrawCommonSupport::CheckSupport()` 内部即 `CD2D1Support1::CheckSupport() && CDWriteSupport::CheckSupport()`（`TaskBarDlgDrawCommon.cpp:337-340`）。

策略：
- `GetAutoFitEnum`（`SupportedRenderEnums.cpp:27`）：**从高到低**检查 bitset（`D2D1 > D2D1_WITH_DCOMPOSITION > DEFAULT`），返回最高位的可用枚举。
- `GetMaxSupportedRenderEnum`（已弃用，`SupportedRenderEnums.cpp:51`）：同样逻辑。

### 5.2 运行期降级

除了构造期静态探测外，还有**运行期错误计数**作为兜底：

- `DrawCommonHelper::DefaultD2DDrawCommonExceptionHandler::m_error_count`（`TaskBarDlgDrawCommon.cpp:86`）是文件级 static 计数器。
- 每次 D2D 渲染抛 `CHResultException` 时构造这个 handler，构造函数里 `++m_error_count`（`TaskBarDlgDrawCommon.cpp:127-131`）。
- 计数超过 `MAX_D2D1_RENDER_ERROR_COUNT` 时（`TaskBarDlgDrawCommon.cpp:140-147`）调用 `HandleErrorCountIncrement`：重置计数 + `m_taskbar_data.disable_d2d = true` + 弹 `IDS_D2DDRAWCOMMON_ERROR_TIP`。
- 由于 `disable_d2d` 写入设置，下次构造 `CSupportedRenderEnums` 时 `D2D1_INDEX` 被设为 false（参见 `EnableDefaultOnly` / `DisableD2D1`，`SupportedRenderEnums.cpp:17-25`），自动退到 GDI。
- 注意：第 147 行的 `MessageBox` 是**非阻塞**警告，不是错误处理流程的一部分；降级通过设置持久化生效。
- `EnableDefaultOnly`（`SupportedRenderEnums.cpp:17`）：把 bitset 重置为 `{1}`，只允许 GDI。
- `DisableD2D1`（`SupportedRenderEnums.cpp:22`）：清掉 D2D1 bit，让 GetAutoFitEnum 自动落到 DComp 或 DEFAULT。

### 5.2 CLazyConstructable / DefaultCLazyConstructableWithInitializer

`Nullable.hpp:217` 提供的延迟构造容器：第一次 `Get()` 时按需 `Construct()`。`CLazyConstructableWithInitializer` 还接受一个 lambda 形式的 init 函数，在 `Get()` 时拉出 tuple 并 `Construct(std::get<Index>(args)...)`。

应用：
- `theApp.m_d2d_taskbar_draw_common_support`（`TrafficMonitor.h:108`）：`CLazyConstructable<CTaskBarDlgDrawCommonSupport>`，任何第一次 `Get()` 调用都会触发 D3D10.1 / D2D1 / DComp 设备的初始化（即 `CTaskBarDlgDrawCommonSupport` 构造里 `RecreateAll()`，`TaskBarDlgDrawCommon.cpp:217-241`）。
- `CTaskBarDlg::m_taskbar_draw_common_window_support`（`TaskBarDlg.h:98-102`）：`DefaultCLazyConstructableWithInitializer<CTaskBarDlgDrawCommonWindowSupport, CTaskBarDlgDrawCommonSupport&>`，init lambda 从 `theApp.m_d2d_taskbar_draw_common_support.Get()` 拉取引用。
- `CTaskBarDlg::m_d2d1_device_context_support`（`TaskBarDlg.h:103-107`）：同理初始化 `CD2D1DeviceContextWindowSupport`。
- `CTaskBarDlgDrawCommon::m_gdi_interop_object`（`TaskBarDlgDrawCommon.h:777`）：按 `m_p_window_support->GetSize()` 构造 GDI 互操作对象。

效果：进程不进入 D2D 渲染路径时，**D3D10.1/D2D1/DComp 设备不会被创建**（除非要让 `CTaskBarDlgDrawCommonSupport::CheckSupport` 返回值改变时主窗口 `CSupportedRenderEnums` 的构造会做静态检查，但不会真创建）。

### 5.3 `DisableRenderFeatureIfNecessary`（TaskBarDlg.cpp:495）

`CTaskBarDlg::DisableRenderFeatureIfNecessary(CSupportedRenderEnums&)` 在 `OnInitDialog` 与 `OpenTaskBarWnd`（`TrafficMonitorDlg.cpp:578`）被调用，规则：
- 任务栏窗口不透明时（`!IsTaskbarTransparent()`）→ `EnableDefaultOnly`。
- `auto_set_background_color = true` → `EnableDefaultOnly`（要按背景色取色，D2D 透明度模型不合适）。
- `disable_d2d = true` → `EnableDefaultOnly`。
- `update_layered_window_error_code != 0`（上次 `UpdateLayeredWindowIndirect` 失败）→ `DisableD2D1`，但保留 D2D1+DComp；仍然 DComp 也不支持时 `EnableDefaultOnly`，并把 `disable_d2d = true`、弹 `IDS_UPDATE_TASKBARDLG_FAILED_TIP` 对话框（`TaskBarDlg.cpp:70-90`）。

### 5.4 `CSupportedRenderEnums::GetAutoFitEnum` 的具体表现

- 不透明 / 自动取色 / 强制 GDI：bitset = `{1}` → `DEFAULT` → `CDrawCommon` + `CDrawDoubleBuffer`。
- 透明 + Win11 任务栏，且 DComp+DXGI1.2 可用：bitset = `{1, 1, 0}` 或 `{1, 0, 1}` → 落到 `D2D1_WITH_DCOMPOSITION`。
- 透明 + D2D1 可用但 DComp 不可用：bitset = `{1, 0, 1}` → `D2D1`，走 `CTaskBarDlgDrawBuffer`（`UpdateLayeredWindowIndirect`）。
- D2D 设备多次失败超过 `MAX_D2D1_RENDER_ERROR_COUNT`（77）：`theApp.m_taskbar_data.disable_d2d = true`（`TaskBarDlgDrawCommon.cpp:138-147`），下次 `OpenTaskBarWnd` 走 DEFAULT。

## 6. DrawTextManager 的角色

`DrawTextManager.h:39` 的 `User32DrawTextManager` 通过 IAT hook 替换 `User32!DrawTextA/W/ExA/ExW` 四个函数。具体替换在 `TaskBarDlgUser32DrawTextHook::Details::ReplacedDrawTextCommon`（`TaskBarDlgDrawCommon.cpp:1177-1360`）：

- 触发条件：`ref_this.m_state.m_on_draw_text_call_matched_hdc == input_hdc`（只对 `CTaskBarDlgDrawCommon` 创建的 GDI 互操作 HDC 起作用，且 `!is_only_calculate_size`）。
- 行为：
  1. 创建一块 ARGB32 DIB（`GetArgb32BitmapInfo` + `CreateDIBSection`，`TaskBarDlgDrawCommon.cpp:1153-1175`）。
  2. 把原 `DrawText*` 调用重定向到这块 DIB 的 DC，强制文字色 `0x00FFFFFF`、底色 `0x00000000`。
  3. 逐像素扫描：若 `RGB 三通道都为 0`，标记为透明；否则按 `alpha = rgb_sum * 0.334f` 输出 `RGBA`。
  4. 以 `AlphaBlend` 把结果贴回原 DC，达到 alpha 文本。
  5. 返回 `User32DrawTextManager::CUSTOM_SUCCESS`（`= 0x7777`，`DrawTextManager.h:130`）让调用方识别。

开关由 `TaskBarDlgUser32DrawTextHook::EnableAllReplaceFunction(state)`（`TaskBarDlgDrawCommon.cpp:1389-1398`）控制，通过 `EnableAllReplaceFunctionGuard`（`TaskBarDlgDrawCommon.h:645`）在 `CTaskBarDlgDrawCommon::ExecuteGdiOperation` 作用域内启用/恢复。

`IAT` hook 由 `User32DrawTextManager::FunctionReplacer`（`DrawTextManager.h:112-127`）通过 `EnableWriteMemoryGuard`（`DrawTextManager.h:7-23`）临时改 `VirtualProtect` 为可写，再 `memcpy` 替换函数指针；原指针保存到 `CommonSettings::m_old_function_pointer`/`m_p_iat_old_function_pointer`。

`CTaskBarDlg::OnPaint` 在抛 `CHResultException`/`CD3D10Exception1`/`CD2D1Exception`/`CDCompositionException`/`std::runtime_error` 时通过 `DrawCommonHelper::HandleIfNeedRecreate`（`TaskBarDlgDrawCommon.h:56-77`）请求设备重建（`TaskBarDlg.cpp:1300-1346`）；`DefaultD2DDrawCommonExceptionHandler`（`TaskBarDlgDrawCommon.h:85`）维护错误计数并 `LogHResultException`。

## 7. DrawCommonEx 与 TaskBarDlgDrawCommon 在任务栏场景的"特殊绘制"

- `CDrawCommonEx` 当前**不**用于任务栏窗口，只在 `CSkinFile::DrawItemsInfo` 中绘制主窗口皮肤的文本（`SkinFile.cpp`）。其位图/边框/线段绘制函数为空实现的事实说明主窗口皮肤绘制路径仅依赖 GDI+ 的 `DrawString` 能力。
- `CTaskBarDlgDrawCommon` 的特殊绘制主要包括：
  - **GDI 互操作 + D2D 合成**：通过 `CGdiInteropObject`（`TaskBarDlgDrawCommon.h:766`）创建与设备上下文同尺寸的 `HBITMAP` + `CDC`；析构时通过 `SetGdiInteropTexture` 把 GDI 内容回灌 GPU 纹理，再以 `DrawAlphaValueReduceEffect`（`ps_4_1` 着色器）把 alpha 规整到 0/1；最后 `CreateSharedBitmap` + `DrawBitmap` 合成到 D2D 设备上下文（`TaskBarDlgDrawCommon.cpp:1684-1718`）。
  - **D2D1 + DirectComposition 路径**：D2D 设备上下文绑到 `CDCompositionHelper` 持有的 `IDXGISwapChain1` 的 `IDXGISurface`（`TaskBarDlgDrawCommon.cpp:1060-1070`），由 `PresentWhenUseDComposition` 提交（`TaskBarDlgDrawCommon.cpp:1074-1078`）。
  - **D2D1 + UpdateLayeredWindowIndirect 路径**：`CTaskBarDlgDrawBuffer`（`TaskBarDlgDrawCommon.h:847`）在析构里 `IDXGISurface1::GetDC` 拿到 GDI 兼容 HDC，构造 `UPDATELAYEREDWINDOWINFO` 并 `UpdateLayeredWindowIndirect`（`TaskBarDlgDrawCommon.cpp:1975-2017`）。失败时把 `GetLastError()` 写入 `theApp.m_taskbar_data.update_layered_window_error_code`，触发 `ShowInfo`/`OnPaint` 的降级。
  - **GDI 文本 alpha 保留**：通过 `User32DrawTextManager` 的 IAT hook + ARGB32 DIB + AlphaBlend，让走 `DrawText*` 的 GDI 文本在 `WS_EX_LAYERED` 任务栏窗口下仍能保留 alpha（`TaskBarDlgDrawCommon.cpp:1177-1360`）。这是 D2D 路径仍能支持插件自绘 GDI 文本的关键。
  - **可绘 emoji**：`m_taskbar_data.enable_colorful_emoji` 打开时 `DrawTextLayout` 调用追加 `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`（`TaskBarDlgDrawCommon.cpp:1805-1806`）。
  - **设备丢失自愈**：`CTaskBarDlgDrawCommon::OnPaint` 五个 catch 块分别调用 `RequestD3D10Device1Recreate`/`RequestD2D1DeviceRecreate`/`RequestDCompositionDeviceRecreate`（`TaskBarDlg.cpp:1300-1346`）；最终 `DefaultD2DDrawCommonExceptionHandler::HandleErrorCountIncrement`（`TaskBarDlgDrawCommon.cpp:138`）按错误次数决定是否永久关闭 D2D。
