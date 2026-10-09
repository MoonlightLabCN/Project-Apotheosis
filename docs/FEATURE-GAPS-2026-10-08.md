# 功能缺口审计 — 2026-10-08

> 子代理静态审计产出（只读，未改代码）。回答两个问题：**哪些功能被 stub / 空实现 / 编译期关掉**，
> 以及**为什么长按选字、复制粘贴、右键菜单用不了**。
> 行号为当前工作副本实测；上游 WebKit 源码在 `E:\Apotheosis\WebKit`（gitignore）。

## 0. 四个结构性前提（决定怎么看后面的表）

**① `PortChromeClient` 原来只在 GPU 成功后才装上 —— 0.2.5.17 已修。**
`port/WebCoreDriver.cpp` buildSession 原来是：
```cpp
if (g_gpuActive) {                      // ← 这一行把 UI 一起挡死了
    auto chrome = ...makeUniqueRefWithoutRefCountedCheck<WebCorePort::PortChromeClient>();
    pageConfiguration.chromeClient = WTF::move(chrome);
}
```
`g_gpuActive` 仅在 `WebCoreGpuInit()` 成功后置 true（初值 false，成功后置 true）。
**GPU 起不来 → 整个页面跑在 WebCore 上游 `EmptyChromeClient` 上**，此时 alert/confirm/prompt/
文件选择器全部变成纯空 stub（`EmptyClients.cpp:99-101`、`:662-664`）。harness 明确保留了这条
软件回退路（`harness/MainPage.xaml.cpp`"GpuInit 失败 → 同一次导航照旧走 Cairo 软件路径"）。

> **0.2.5.17 修复**：ChromeClient 改为**无条件装配**，把 gate 从"整个 client"下沉到"需要真合成
> 的那几个钩子"（`PortChromeClient::requestPresent()` 经新的
> `setCompositingPathActive(g_gpuActive)` 控制；`WebCoreGpuInit` 成功时也会置位，
> 让已建好的软件会话立刻开始呈现）。
> 之所以安全：PortChromeClient 的 UI 工作全走 PortUIBridge 异步请求，**不碰 GL/EGL/ANGLE**；
> 而合成开关（`setAcceleratedCompositingEnabled` / `setForceCompositingMode`）本来就已经单独
> gate 在 `g_gpuActive` 上，原来这层 chromeClient 门是多余的。
> 逐项核对过与 `EmptyChromeClient` 的**合成侧**差异，全部等价：
> `scheduleRenderingUpdate()` 上游默认就是 `false`（PortChromeClient 也返回 false）；
> `attachRootGraphicsLayer` 只在开合成时才被调（软件路径收到 null 或不调）；
> `triggerRenderingUpdate` / `setNeedsOneShotDrawingSynchronization` /
> `didFinishLoadingImageForElement` 都收口在 `requestPresent()`，gate 后就是空操作。
> `allowsAcceleratedCompositing()` / `allowedCompositingTriggers()` / `graphicsLayerFactory()` /
> `isEmptyChromeClient()` 与上游默认**逐值相同**。→ 软件路径的 present 行为逐字节不变，
> 这是在 0.1.7.1 闪退教训之后敢动这条路径的前提。
> **待真机验证**：软件回退下 alert / confirm / prompt / 文件选择 / select / color / date 可用。

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

⚠️ 上表的 `MediaPlayerPrivateMediaFoundation*.cpp` 被 DROP **不等于没有媒体播放器**：2026-10-09 起
新增了自写的 `MediaPlayerPrivateWinUWP.cpp`（MF `SourceReader` 路线），并已列进 `PlatformWinUWP.cmake`
的编译源。别按「媒体被删了」去读这一条，见 §5。

---

## 1. 主表（按用户最容易碰到排序）

| 类别 | 功能 | 状态 | 位置 | 用户可见影响 |
|---|---|---|---|---|
| 交互 | 右键/长按上下文菜单 | ✅ 0.2.5.15 已接（真机待验） | 引擎侧 `ENABLE_CONTEXT_MENUS 0` 仍关着，但壳做原生卡（复制/全选/分享/新标签开）覆盖了主要场景 | UA 菜单链仍整体不存在；壳卡已可覆盖 90% |
| 交互 | 长按菜单的复制/选字/存图 | ✅ 0.2.5.15 已接（真机待验） | `harness/MainPage.xaml.cpp` 长按菜单 + 引擎侧复制粘贴 | 复制/全选可用；**选字仍不可用**（无选中几何回传通道，见 §4） |
| 剪贴板 | 复制/剪切/粘贴 | ✅ 0.2.5.15 已接（真机待验） | `stubs-pasteboard.cpp` 全打通 + `WebCoreCopySelection/Paste/SelectAll` + UWP DataPackage 搬运 | 引擎链活了。⚠️ 仅纯文本；图片/颜色/自定义数据写侧仍 no-op |
| 表单 | `<select>` 下拉 | ✅ 0.2.5.15 已接（真机待验） | `PortChromeClient::createPopupMenu` → `PortUIBridge` `UIRequestSelect=5` + 壳 ListView | 弹原生列表、能选 |
| 表单 | datalist / color / date-time | ✅ 0.2.5.16 已接（真机待验）；datalist 仍 null | `UIRequestColor=6` / `UIRequestDateTime=7` + 壳色板/日期卡 | color 与 date/time 可用；**`<input list>` 建议列表仍无** |
| 文件 | `<input type=file>` | ✅ 0.2.5.17 起软件路径也可用 | `PortChromeClient.cpp:69-72`→`PortUIBridge` + `FileOpenPicker`；原先软件路径被 `if (g_gpuActive)` 挡死 | 两路径都能选文件（0.2.5.17 修复了软件回退死 UI 的问题，见 §0①） |
| 对话框 | alert / confirm / prompt | ✅ 0.2.5.17 起软件路径也可用 | 真 `PortChromeClient.cpp:86-118`（confirm/prompt 是真模态：引擎 park 条件变量，60s 超时） | 两路径都正常（软件路径原先直接返回 false、用户什么都看不到） |
| 弹窗 | `window.open()` | 半实现，返回 null | `PortChromeClient.cpp:122-134` | 新标签能开但 `open()` 返回 null → `w.document.write()` 抛异常；**无 opener** → OAuth 弹窗登录不工作 |
| 媒体 | `<video>` / `<audio>` | 🔶 引擎侧已落地，**真机待验** | `ENABLE_VIDEO=1`（开发线）+ `MediaPlayerPrivateWinUWP.cpp`（MF SourceReader→RGB32）+ 壳侧 AudioGraph；⚠️ 根因曾是我们自己 `settings.mediaEnabled(false)`，见 §5 | 元素现在是真 `HTMLVideoElement` 了（原先一直是 `HTMLUnknownElement`）。画面/声音**尚未在真机证实**；MSE/DASH（B站/抖音）仍不播，因 `ENABLE_MEDIA_SOURCE=0` |
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
`WEB_AUDIO · MEDIA_SOURCE · MEDIA_STREAM · MEDIA_CAPTURE · MEDIA_RECORDER ·
WEB_RTC · ENCRYPTED_MEDIA · NOTIFICATIONS · GEOLOCATION · FULLSCREEN_API · CONTEXT_MENUS ·
DRAG_SUPPORT · TOUCH_EVENTS · POINTER_LOCK · WEBGL · WEBGPU · WEBXR · WEBASSEMBLY ·
SPEECH_SYNTHESIS · SPELLCHECK · GAMEPAD · DEVICE_ORIENTATION · ORIENTATION_EVENTS · XSLT ·
MATHML · WEB_AUTHN · PAYMENT_REQUEST · APPLICATION_MANIFEST · MEDIA_SESSION · PDFJS/PDFKIT ·
REMOTE_INSPECTOR · WEB_CODECS · MHTML · DARK_MODE_CSS · TEXT_AUTOSIZING · ASYNC_SCROLLING ·
CACHE_PARTITIONING · VARIATION_FONTS`
开着：`VIDEO（2026-10-09 起，开发线 build-clang-gpu）· SMOOTH_SCROLLING · USER_MESSAGE_HANDLERS ·
JAVASCRIPT_SHELL · JIT(gpu/jit) · USE_CAIRO · USE_ANGLE · USE_TEXTURE_MAPPER · USE_CURL ·
USE_OPENSSL · USE_FREETYPE/FONTCONFIG/HARFBUZZ · USE_THEME_ADWAITA`

⚠️ 清单对应 `build-clang-gpu`（开发线）。`build-clang-webcore` / `build-clang-jit` 两个目录的
`ENABLE_VIDEO` 仍是关的；`ENABLE_VIDEO` 是 CMake **默认值**，翻转后必须显式 reconfigure
（`-DENABLE_VIDEO=ON`）才生效——`WEBKIT_OPTION_DEFAULT_PORT_VALUE` 不会改已有的 `CMakeCache.txt`。

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

> 本节的 1–3 已于 2026-10-08 全部完成并提交（见 `docs/HANDOFF-2026-10-08.md`）；
> 保留原文是为了记录"为什么当初是这些顺序"。

1. ~~**让软件回退路径也用 `PortChromeClient`**~~ → **0.2.5.17 已完成**（gate 下沉到
   合成钩子，见 §0①）。一行门同时救活：文件选择器 + JS 对话框（alert/confirm/prompt）。
2. ~~**`<select>` 下拉**~~ → **0.2.5.15 已完成**（`UIRequestSelect=5`，壳做 XAML ListView 弹卡，
   照 FileChooser 的 request/response + staleness 模式）。同轮还做了 `<input type=color>`
    (kind 6) 与 `<input type=date/time>` (kind 7, 0.2.5.16)。
3. ~~**剪贴板写侧接 UWP `DataTransfer::Clipboard`**~~ → **0.2.5.15 已完成**
   （stubs-pasteboard.cpp 全打通 + `WebCoreCopySelection`/`WebCorePaste`/
   `WebCoreSelectAll`，长按菜单出复制/全选/分享）。
4. **真机验证 Service Worker 崩溃风险**（§1 那条 `RELEASE_ASSERT`）。
5. **`DisplayRefreshMonitor` 给一个 60Hz 假 monitor**（现在 rAF 走一次性 Timer，帧率不稳），
   比接真 vsync 便宜得多。
6. **`NetworkStateNotifier` 无 change 通知**：`startObserving()` 空 + App Container 无
   `NotifyAddrChange` → 掉线上线事件不到页面，`navigator.onLine` 只在 query 时刷新。

---

## 5. 视频 / 音频 / 硬解（2026-10-09 实测更新）

### 先纠偏：这一节的旧前提是错的

本节原先写「`ENABLE_VIDEO=0` … → `<video>` 渲染成空框、`.play()` 不报错但不出内容」。**症状描述对了，归因错了。**
真机实测（2026-10-09）确认：`<video>` 之所以是空框，主因不在编译开关，而在**移植驱动自己**：

```cpp
// port/WebCoreDriver.cpp 建页时
page->settings().setMediaEnabled(false);
```

生成的 `WebCore/DerivedSources/HTMLElementFactory.cpp` 里正是这一句决定元素类型：

```cpp
if (!document.settings().mediaEnabled())
    return HTMLUnknownElement::create(tagName, document);
return HTMLVideoElement::create(tagName, document, createdByParser);
```

所以 `<video>` 一直是 **`HTMLUnknownElement`** —— 不是媒体元素：从不创建 `MediaPlayer`、从不取流，
`currentTime`/`readyState`/`load` 都不存在，`.play()` 在普通元素上是空操作。**`ENABLE_VIDEO=1` 是必要条件但远远不够，port 还必须主动把这个 setting 打开。**

设备自报的判别证据（`source` 只受 `#if ENABLE(VIDEO)` 门控、**不查**该 setting，是天然判别器）：

```
video=HTMLUnknownElement   audio=HTMLUnknownElement   track=HTMLUnknownElement
source=HTMLSourceElement   canvas=HTMLCanvasElement    win.HTMLVideoElement=function
```

`source` 正常 ⇒ 媒体分支确实编进去了 ⇒ 失败点是**运行期 setting**。已修（提交 `78437c4`，分支 `media-mf`），
随 0.2.5.19 打包。

### 当前实际状态

| 项 | 状态 |
|---|---|
| `ENABLE_VIDEO` | **ON**（开发线 `build-clang-gpu`；`port/configure-gpu.ps1` 显式 `-DENABLE_VIDEO=ON`） |
| `MediaPlayerPrivateInterface` 实现 | **已写**：`platform/graphics/win/MediaPlayerPrivateWinUWP.cpp`（MF `MFCreateSourceReaderFromURL` + `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING` → RGB32 → CPU BGRA → 复用既有 `drawNativeImage` 呈现路径） |
| `settings.mediaEnabled` | **已修 true**（`buildSession` / `WebCoreLoadUrl`；`WebCoreRenderHtml` 离屏快照路径刻意保持 false） |
| 音频输出 | 已在引擎侧留出浮点 PCM 环 + `WinUWPMediaAudioSink` 接口，壳侧 `harness/MediaAudio.cpp` 用 AudioGraph 消费（`IMemoryBufferByteAccess` 填 `AudioFrame`） |
| Media Controls UI | 仍未做（`modern-media-controls` 依旧 DROP）→ 计划是壳侧 XAML 浮层，不恢复 Shadow DOM 控件 |
| **画面 + 声音（真机）** | **待验证** —— 0.2.5.19 已打包但未装上（装 54MB 途中手机息屏，WDP 被省电杀掉）。见 `docs/MEDIA-MF-IMPLEMENTATION.md` 的 runbook |
| 硬解 DXVA2 | 走 MF Video Processor，App Container 内可用性**待实测**；`openSourceReader()` 里留了 `MF_SOURCE_READER_DISABLE_DXVA = TRUE` 一行开关可退回软解 |

### 明确不在范围（别再当成"缺口"来修）

- **MSE/DASH/HLS：`ENABLE_MEDIA_SOURCE=0`**，实测 `window.MediaSource === undefined`。这直接决定
  **B 站/抖音这类页面不会播**：它们全是 MSE/DASH 播放器。实测数据——B 站视频页加载**完全正常**
  （`firstbyte=161 commit=654 fp=2298 load=4072`、`subres=30/30`、无 JS 报错），只是引擎从未被请求
  （`stage.txt` → `media readers=0 frames=0/0`）。**这是预期行为，不是回归。** 要支持它得单开一轮做 MSE。
- **Web Audio**（`ENABLE_WEB_AUDIO=0`）：与 `<video>` 出声无关（壳侧走原生 AudioGraph），暂不动。
- **WebRTC**：不做。
- 媒体加载**只能走渐进式 `FileOrHLS`**：`MediaPlayerPrivateWinUWP::supportsType()` 对其余
  `platformType` 直接返回 `IsNotSupported`，让元素快速失败而不是挂住。

### 历史遗留的可行性判断（多数已落地）

原先列的 4 条里，1（开开关）与 2 的前半（写 `MediaPlayerPrivateInterface`）已完成；3 的音频输出走的是
AudioGraph 而非 WASAPI/XAudio2；4（controls）仍未做。现在**真正的未知数只剩一个**：App Container 里
`MFStartup` / `MFCreateSourceReaderFromURL` / Video Processor MFT 是否都被允许 —— 这正是设备回合要回答的。

