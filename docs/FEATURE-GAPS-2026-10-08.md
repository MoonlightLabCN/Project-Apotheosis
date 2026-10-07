# 功能缺口审计 — 2026-10-08

> 子代理静态审计产出（只读，未改代码）。回答两个问题：**哪些功能被 stub / 空实现 / 编译期关掉**，
> 以及**为什么长按选字、复制粘贴、右键菜单用不了**。
> 行号为当前工作副本实测；上游 WebKit 源码在 `E:\Apotheosis\WebKit`（gitignore）。

## 0. 四个结构性前提（决定怎么看后面的表）

**① `PortChromeClient` 只在 GPU 成功后才装上。** `port/WebCoreDriver.cpp:3864-3868`：
```cpp
if (g_gpuActive) {
    auto chrome = ...makeUniqueRefWithoutRefCountedCheck<WebCorePort::PortChromeClient>();
    pageConfiguration.chromeClient = WTF::move(chrome);
}
```
`g_gpuActive` 仅在 `WebCoreGpuInit()` 成功后置 true（初值 false @`:685`，置 true @`:7189`）。
**GPU 起不来 → 整个页面跑在 WebCore 上游 `EmptyChromeClient` 上**，此时 alert/confirm/prompt/
文件选择器全部变成纯空 stub（`EmptyClients.cpp:99-101`、`:662-664`）。harness 明确保留了这条
软件回退路（`harness/MainPage.xaml.cpp:7989`"GpuInit 失败 → 同一次导航照旧走 Cairo 软件路径"）。
→ **表里所有标"GPU 路径真实现"的功能，在软件回退路径上实际是死的。**

**② `PortChromeClient` 直接继承 `ChromeClient`**（不是 `EmptyChromeClient` 子类），自己实现全部
纯虚函数；真正还在用 `EmptyChromeClient` 默认实现的是软件/一次性渲染路径（`WebCoreRenderHtml`
@`WebCoreDriver.cpp:4774`、`WebCoreLoadUrl`:4933、`WebCoreRenderHtmlStub`:7906）。

**③ 三份 cmakeconfig 的 WebCore `ENABLE_*` 逐个相同**，唯一差异是 `ENABLE_JIT`
（webcore=0 / jit=1 / gpu=1）；`ENABLE_DFG_JIT`、`ENABLE_FTL_JIT` 全为 0。关掉的完整清单见 §1 顶部。

**④ 权威的"哪些平台源码没编"在 `WebKit/Source/WebCore/PlatformWinUWP.cmake`** 的
EXPLICITLY DROPPED 段（逐条带了原因）。已用 `build-clang-gpu/build.ninja` + unified-sources
交叉验证。被 DROP 的包括：`EditorWin.cpp`、`PasteboardWin.cpp`、`StaticPasteboard.cpp`、
`WCDataObject.cpp`、`ClipboardUtilitiesWin.cpp`、`HTMLSelectElementWin.cpp`、
`DragControllerWin.cpp`、`PlatformScreenWin.cpp`、`SharedMemoryWin.cpp`、`IconWin.cpp`、
`SystemFontDatabaseWin.cpp`、`DIBPixelData.cpp`、`GDIUtilities.cpp`、
`DisplayRefreshMonitorWin.cpp`、`FullScreen*.cpp`、`MediaPlayerPrivateMediaFoundation*.cpp`、
`accessibility/win/AX*Win.cpp`、`MainThreadSharedTimerWin.cpp`。

---

## 1. 主表（按用户最容易碰到排序）

| 类别 | 功能 | 状态 | 位置 | 用户可见影响 |
|---|---|---|---|---|
| 交互 | **右键/长按上下文菜单** | **编译期关掉** | `cmakeconfig.h:37` `ENABLE_CONTEXT_MENUS 0`；`EmptyClients.cpp:134-169`（`EmptyContextMenuClient` 整段被 #if 包住） | UA 菜单链整体不存在。`ContextMenuController.cpp:30-1957` 全不编译、`Page.cpp:400-401` 不构造 controller |
| 交互 | 长按菜单的复制/选字/存图 | 明确未做 | `harness/MainPage.xaml.cpp:5875-5878`（作者自记："Text selection, copy link and save image are deliberately NOT here"） | 长按链接只有一个"在新标签页打开" |
| 剪贴板 | 复制/剪切/粘贴 | **stub 全 no-op** | `stubs-pasteboard.cpp:65-221` | 复制无效果、粘贴无效、`navigator.clipboard.readText()` 空。⚠️ 但壳能自己复制 URL（`MainPage.xaml.cpp:6157-6166 DoCopyLink` 走 UWP `DataTransfer::Clipboard`） |
| 表单 | `<select>` 下拉 | **stub 返回 nullptr** | `PortChromeClient.cpp:32-40` | 点了不弹。已有 null 保护（`HTMLSelectElement.cpp` 判空）不崩。唯一可用路是 `appearance: base-select` 的 DOM popover |
| 表单 | datalist / color / date-time | stub nullptr | `PortChromeClient.cpp:42-55` | 无建议列表、无取色面板、无日期滚轮 |
| 文件 | `<input type=file>` | **⚠ GPU 真 / 软件 stub** | 真 `PortChromeClient.cpp:69-72`→`PortUIBridge.h:77`+`FileOpenPicker`；stub `EmptyClients.cpp:662-664` | **只有 GPU 起成功才能选文件**。GPU 依赖最要命的一条 |
| 对话框 | alert / confirm / prompt | **⚠ GPU 真 / 软件 stub** | 真 `PortChromeClient.cpp:86-118`（confirm/prompt 是真模态：引擎 park 条件变量，60s 超时）；stub `EmptyClients.cpp:99-101` | 软件路径 confirm/prompt 直接返回 false，用户什么都看不到 |
| 弹窗 | `window.open()` | 半实现，返回 null | `PortChromeClient.cpp:122-134` | 新标签能开但 `open()` 返回 null → `w.document.write()` 抛异常；**无 opener** → OAuth 弹窗登录不工作 |
| 媒体 | `<video>` / `<audio>` | **未开** | `cmakeconfig.h:121,142,195` `USE_MEDIA_FOUNDATION 0`；`PlatformWinUWP.cmake` DROP `MediaPlayerPrivateMediaFoundation*.cpp` | 空框无声，YouTube/B站全废 |
| 触控 | **Touch / Pointer Events** | **未开** | `cmakeconfig.h:116` `ENABLE_TOUCH_EVENTS 0`、`:96` POINTER_LOCK 0 | 只合成鼠标事件。**手机浏览器上最刺眼** |
| 3D | WebGL / WebGPU / WebXR | 未开 | `cmakeconfig.h:134-140` | ANGLE 只服务合成，不开放给 WebGL |
| 通信 | WebRTC / getUserMedia / 录制 | 未开 | `cmakeconfig.h:69,71,78,145` | 无任何音视频通话 |
| 下载 | `download` / attachment | no-op 静默丢弃 | `LoadingFrameLoaderClient.cpp:531-533`、`:338-340` | 点下载无反应也无报错。壳只有独立的 `WebCoreDownload` C ABI（另起 curl） |
| 网络 | HTTP 401 Basic/Digest | no-op | `LoadingFrameLoaderClient.cpp:355-357`、`:346-349`、`:361-363` | 受密码保护的内网页、路由器管理页无法登录 |
| 通知/位置/全屏 | Notification / Geolocation / Fullscreen | 未开 | `cmakeconfig.h:85,52,50` | API 全部不存在 |
| 拖拽 | HTML5 drag & drop | 未开 + DragImage stub | `cmakeconfig.h:45`；`stubs-other.cpp:47-64` | draggable/拖文件全不可用。⚠️ harness 的 `WebCoreWantsDragAt`/`WebCoreDragAt` 是把拖拽当指针事件滚动，不是 HTML5 DnD |
| 离线 | **Service Worker** | **⚠ 疑似崩溃** | `ServiceWorkerProvider.cpp:43-48` `RELEASE_ASSERT(sharedProvider)`；port 从未调用 `setSharedProvider()`（全文 0 处） | `navigator.serviceWorker.register()` 可能直接杀 app（setting 默认 false 使加载路径不崩）。**建议真机验证** |
| 无障碍 | Accessibility（Narrator） | stub 全 no-op | `port/stubs-ax.cpp`（整文件 82 行） | 屏幕阅读器读不出内容。根因：`accessibility/win/AX*Win.cpp` 整个 DROP |
| 渲染 | DisplayRefreshMonitor（vsync） | stub → 优雅降级 | `stubs-other.cpp:304-307`（`create()→nullptr`） | rAF/CSS 动画仍跑，但走一次性 Timer 而非 vsync → 帧率不稳 |
| 字体 | `font: caption/menu/status-bar` | stub 返回空 | `stubs-other.cpp:290-297` | 解析成空 family → 回落默认字体 |
| 打印/分享/查词/光标 | print / share / TextIndicator / cursor | no-op | `PortChromeClient.h:191,213`；`PortChromeClient.cpp:57-63,136-138`；`stubs-other.cpp:70-77` | 触屏上 mostly 无害 |
| 插件 | NPAPI/Flash | 无 | `EmptyClients.cpp:528-532`、`:721-724` | 无外部插件内容 |

### 编译期关掉的完整 ENABLE_* 清单（`cmakeconfig.h`）
`VIDEO · WEB_AUDIO · MEDIA_SOURCE · MEDIA_STREAM · MEDIA_CAPTURE · MEDIA_RECORDER ·
WEB_RTC · ENCRYPTED_MEDIA · NOTIFICATIONS · GEOLOCATION · FULLSCREEN_API · CONTEXT_MENUS ·
DRAG_SUPPORT · TOUCH_EVENTS · POINTER_LOCK · WEBGL · WEBGPU · WEBXR · WEBASSEMBLY ·
SPEECH_SYNTHESIS · SPELLCHECK · GAMEPAD · DEVICE_ORIENTATION · ORIENTATION_EVENTS · XSLT ·
MATHML · WEB_AUTHN · PAYMENT_REQUEST · APPLICATION_MANIFEST · MEDIA_SESSION · PDFJS/PDFKIT ·
REMOTE_INSPECTOR · WEB_CODECS · MHTML · DARK_MODE_CSS · TEXT_AUTOSIZING · ASYNC_SCROLLING ·
CACHE_PARTITIONING · VARIATION_FONTS`
开着：`SMOOTH_SCROLLING · USER_MESSAGE_HANDLERS · JAVASCRIPT_SHELL · JIT(gpu/jit) ·
USE_CAIRO · USE_ANGLE · USE_TEXTURE_MAPPER · USE_CURL · USE_OPENSSL · USE_FREETYPE/FONTCONFIG/
HARFBUZZ · USE_THEME_ADWAITA`

---

## 2. 关键纠偏：看起来像 stub，其实是真的

| 符号 | 位置 | 实际 |
|---|---|---|
| `PAL::CryptoDigest` | `stubs-crypto.cpp:36-87` | **真实现**（OpenSSL EVP，SHA1/224/256/384/512）。早年返回全 0 已修；`m_context` 为 null（OOM）时返回空 Vector 让 SRI fail-closed |
| `PublicSuffixStore::platform*` | `stubs-other.cpp:149-450` | **真实现**：完整 publicsuffix.org 算法（最长匹配、`*.` 通配、`!` 例外、隐式规则），数据由 `WebCoreSetPublicSuffixListBlob` 注入 |
| `crypto.subtle` | `PlatformWinUWP.cmake` include OpenSSL.cmake | **真实现**：18 个 TU，SHA×4+HMAC+PBKDF2+HKDF+AES 全族+ECDH/ECDSA+RSA 全族 |
| 网络栈 / localStorage / IndexedDB / WebSocket / cookie jar | `stubs-network.cpp`、`PortWebSocket.cpp`、`PortStorage.cpp`、`PortNetworkStorageSession.cpp` | **全真实现**（curl+OpenSSL、SQLite、自研 WS 协议层）。`stubs-network.cpp` 已瘦身到只剩 2 个符号，其中 `NetworkStateNotifier` 用 `InternetGetConnectedState()` 真查 |
| `GraphicsLayer::create()` | `stubs-other.cpp:320-325` | 仅 `#if !USE(TEXTURE_MAPPER)` 才是 RELEASE_ASSERT；GPU 构建有 `GraphicsLayerTextureMapper.cpp` 真实现 |
| `NullGraphicsContext` / `PlatformMediaSession(Manager)` | `platform/graphics/NullGraphicsContext.h`、`platform/audio/*` | **上游通用设施，非本 port 替身**（前者全在头文件、无 .cpp）；后者虽是死代码但不是"被替身" |
| `EmptyClients.cpp` / `EmptyChromeClient` | 上游原生文件 | **不是本 port 写的 stub**；但本 port 在软件路径上确实直接使用它 |
| `WCDataObject::Release()` | `stubs-pasteboard.cpp:196-201` | 真是 no-op，但只在该 stub 类内部可达 |

---

## 3. 文本选择 / 复制粘贴 / 右键菜单 —— 为什么用不了

**结论：四项全不可用，且多数是刻意决定而非遗漏。引擎内部的 selection/editing 机器编译进来、
能跑；断点全在 port/harness 的触发层和 pasteboard 终点。**

### 长按选字 —— 断点有三层
1. **harness 分流**（`MainPage.xaml.cpp:2968`）：长按先走 `WebCoreLinkAt` 探针，命中链接 → 弹
   LinkMenu 卡，**不向页面派发任何事件**；非链接才调 `WebCoreLongPressAt(... CONTEXTMENU |
   DRAG_WIDGET_ONLY ...)`。
2. **引擎早退**（`WebCoreDriver.cpp:6261-6271`）：`dragWidgetAtPoint()==false` 且带 flag 4 →
   立即 `return kOK`，mousedown / contextmenu 什么都没派。**这是设计决定**：`:2830-2832` 注释
   "a hold over ordinary article text must keep doing nothing, or press-hold-release over a link
   would open it"。设备日志佐证（`:965-966`）：map canvas 上 `ctx=1/1`——该路径只在 drag widget 上活。
3. **选词逻辑双不满足**：即使 contextmenu 送到文字上，`EventHandler.cpp:3902-3907` 的自动选词
   要求 `shouldSelectOnContextualMenuClick()`，而它是 **Mac-only**（`EditingBehavior.h:62`）；
   本 port 是 `EditingBehaviorType::Windows`（`Settings.yaml:101-105`）。

另外：harness 从不在文字上发起 drag（manipulation 分流只在 `WebCoreWantsDragAt()==1` 时走
`WebCoreDragAt`，其余一律滚屏 `:3457`），所以引擎里活着的 `updateSelectionForMouseDrag`
（`EventHandler.cpp:1136`，**无 DRAG_SUPPORT 门控**）和 `RenderObject::positionForPoint`
系列**从来没有被触发过**。

### 上下文菜单 —— 编译期整体不存在
2.52 的 `ChromeClient` **没有** `showContextMenu` 方法；机制是 `ContextMenuController` +
`ContextMenuClient`（`page/ContextMenuController.h:50`、`page/ContextMenuClient.h:43-71`）。
`ENABLE_CONTEXT_MENUS=0` 导致：`ContextMenuController.cpp` 全不编译、`Page.cpp` 不构造
controller、`PageConfiguration.cpp` 的成员都不存在。port 下无自定义 ContextMenu 客户端。
**唯一存在的菜单是 harness 原生 LinkMenu 卡**（`MainPage.xaml:1048-1072`、`ShowLinkMenu` @
`MainPage.xaml.cpp:5904`），只覆盖长按链接、只有一个动作。鼠标右键本体无 `RightTapped` 处理器。

### 复制粘贴 —— 断点在 pasteboard 终点与入口层
WebCore 主链编译在位：`execCommand` → `Document::execCommand` → `Editor::command`
（`EditorCommand.cpp:1888-1896`）→ copy/cut → `Editor.cpp:1607 performCutOrCopy` → `:1620/:1639`
→ **stub no-op**；paste → `:1655` → `:1671 pasteWithPasteboard` → **stub no-op**；DOM clipboard
事件的事件内 `clipboardData` 是 in-memory `StaticPasteboard`（**该文件确实编进了库**，
`Editor.cpp:470-487` 可用——更正了 `PlatformWinUWP.cmake:386` 与 `stubs-pasteboard.cpp:9`
注释里"已 drop"的说法），但 `Editor.cpp:507 commitToPasteboard` 落到 stub 基类，出不了引擎。
**入口层为零**：C ABI 无任何 clipboard/selection 导出；无 Ctrl+C/V 通道；输入只有
`WebCoreTypeText`/Enter/Backspace。`selectAll` 是纯 FrameSelection、链路可用，但**没有任何用户入口**。

### 拖拽选择手柄 —— 不存在
port/ 与 harness/ 都未找到实现（`PortChromeClient.cpp:57-63` 的 `setTextIndicator` 是 WebKit2
look-up 预览用的，不是选择手柄）。WebCore 本身没有"选择手柄"概念（UI 层功能）。
**最重的缺口：目前没有任何选中几何回传通道**——harness 只有 RGBA 帧，不知道选中矩形在哪。

---

## 4. 建议优先补的（按投入产出）

1. **让软件回退路径也用 `PortChromeClient`**（`WebCoreDriver.cpp:3864` 那行 `if (g_gpuActive)`
   是唯一门）。一行门同时救活：文件选择器 + JS 对话框（alert/confirm/prompt）。
2. **`<select>` 下拉**：`PortUIBridge.h:65` 已预留 `UIRequestSelect=5`，壳做 XAML ListView 弹卡；
   FileChooser 的 request/response + staleness 模式已经趟通，可照抄。
3. **剪贴板写侧接 UWP `DataTransfer::Clipboard`**（壳 `DoCopyLink` 已证明可用），读侧同理；
   再补 `WebCoreCopySelection`/`WebCorePaste`/`WebCoreSelectAll` C ABI（**两份
   `WebCoreDriver.h` 必须同步**）。这是手机上最常用的操作。
4. **真机验证 Service Worker 崩溃风险**（§1 那条 `RELEASE_ASSERT`）。
5. **`DisplayRefreshMonitor` 给一个 60Hz 假 monitor**（现在 rAF 走一次性 Timer，帧率不稳），
   比接真 vsync 便宜得多。
6. **`NetworkStateNotifier` 无 change 通知**：`startObserving()` 空 + App Container 无
   `NotifyAddrChange` → 掉线上线事件不到页面，`navigator.onLine` 只在 query 时刷新。

---

## 5. 视频 / 音频 / 硬解可行性（当前状态）

`ENABLE_VIDEO=0` / `ENABLE_WEB_AUDIO=0` / `USE_MEDIA_FOUNDATION=0`，且
`MediaPlayerPrivateMediaFoundation*.cpp` 与 `modern-media-controls` 资源被 DROP → `<video>` /
`<audio>` 渲染成空框、无画面无声，`.play()` 不报错但不出内容。

**要真放视频，需要：**
1. `ENABLE_VIDEO=1` + `USE_MEDIA_FOUNDATION=1`（Win10M 自带 Media Foundation，是硬解的正路，
   比自接 ffmpeg 现实得多）；
2. 从零写 `MediaPlayerPrivateInterface` 实现：MF source resolver → `MFT` 解码 transform
   （Lumia 950 的 Adreno 420 走 DXVA2 硬解 H.264）+ 一个 EFX/video 处理器或直接
   `EVRAsyncCallback`/`SimpleVideoWindow` 呈现到我们的 surface；
3. 音频输出：`IAudioClient`（WASAPI）或 XAudio2；可以先只解不出声验证链路；
4. Media Controls UI 需要 `modern-media-controls` 资源（被 DROP 了，要恢复）。

**工作量判断：这是"单独一轮"的工程**，建议先做"一个 `<video>` 测试页放本地 mp4，能出画面+出声"
的最小闭环，别和崩溃修复搅在一起。App Container 下 MF 硬解 transform 可用性需实测。
