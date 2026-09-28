# Capabilities 清单（供 bug 扫描用）

> 生成于 2026-09-27。描述对象：`Harness_0.2.0.0_ARM.appx`（WebKit 2.54.0 重基线后的
> 全量重编产物）。本文是能力/接缝/风险面的**地图**，按子系统列出"承诺了什么、在哪实现、
> 哪里最可能藏 bug"。代码扫描时请把第 4、5 节当重点。

---

## 1. 交付物与构建形态

| 项 | 值 |
|---|---|
| appx | `harness/AppPackages/Harness/Harness_0.2.0.0_ARM_Test/Harness_0.2.0.0_ARM.appx`（62 MB） |
| 目标平台 | Windows 10 Mobile / Lumia 950，ARM32（thumbv7），UWP App Container |
| MinVersion | 10.0.14393.0（Package.appxmanifest） |
| 引擎 | WebKit 2.54.0（`wk-winuwp.patch`：167 文件 +13703/−108，与树 md5 一致） |
| JS 引擎 | **LLInt 解释器**（JIT 关；`-DAPOTHEOSIS_JIT` 门控已接好但 2.54 的 32 位 JIT 适配未完成，见 docs/JIT-254-PORTING.md） |
| 内存分配 | USE_SYSTEM_MALLOC（bmalloc 仅编译不链；FastMalloc 走 `_aligned_malloc` 配对实现） |
| 渲染 | 软件：Cairo→RGBA→WriteableBitmap；GPU：TextureMapper 同步合成 + ANGLE(D3D11 FL9_3) → SwapChainPanel（APOTHEOSIS_GPU） |
| 网络 | 自带 curl + OpenSSL 1.x（TLS1.3），不依赖系统网络栈 |
| 构建入口 | `port/configure-gpu.ps1` → `port/build-engine-gpu.ps1`（ninja）→ `port/link-driver-gpu.ps1` → `port/build-harness.ps1`（XamlMode=Fallback 手搓 codegen） |

## 2. 能力矩阵

### 2.1 JS 与脚本
- ES 模块、Promise/微任务（驱动显式排 `performMicrotaskCheckpoint`）、rAF/定时器、WebAssembly（ENABLE_WEBASSEMBLY=0，关闭）
- WebCrypto：OpenSSL 后端（ECDSA/HKDF/RSA…；EC_KEY const-cast 适配点见 crypto/openssl 补丁）
- console 输出镜像到 `LocalState\console.txt`
- 对话框（2026-09-27 起）：`confirm()` 真模态——引擎线程 park 在 PortUIBridge 条件变量上，UI 线程 MessageDialog 回填，60s 超时按取消；`prompt()` 同链路 + 自绘输入框（PromptBox overlay：消息/TextBox/确定/取消，遮照点击=取消）；`alert()` 仍是非阻塞通知（设计如此）
- **风险点**：LLInt-only 语义（无 JIT 的时序/内存差异）；微任务排空点在驱动侧（漏排/重排）

### 2.2 网络与会话
- 导航/重定向/HTTP2、SRI、DNS 预取（prefetchDNS→scheduler）、preconnect、IPv4-only 开关（`WebCoreSetIPv4Only`）
- 主资源传输失败**自动重试一次**（限快速失败的 PROVISIONAL 传输错；`isRetriableTransportError`）
- Cookie：JSON Lines 持久化（`WebCoreSetCookieJsonPath`），SameSite 全链路（0.2.0：CookieUtil/CookieJarDB/NetworkStorageSession 三处 WK_WINUWP 补齐 + 快照含 sameSite 字段，旧快照按 Unspecified 读回）
- Public Suffix List：**PR 的 blob 方案**（`WebCoreSetPublicSuffixListBlob` 启动注入，fetch-publicsuffix.ps1 SHA 钉死）；本地表方案（PublicSuffixStorePort）保留在树里但不进编译
- WebSocket：PR 的 PortWebSocketChannel（CurlStreamScheduler 底座，含 permessage-deflate/关闭握手）；WebTransport 明确拒绝
- **风险点**：cookie 的 registrable-domain 语义（PSL 与 jar 的交界）；重试逻辑的边界（何时重试/何时不）；ws 握手的 cookie/SameSite 组合

### 2.3 存储
- localStorage / IndexedDB：profile 目录持久化（`WebCoreSetProfilePath` → `<profile>\storage|indexeddb`），SQLite win32 VFS 补丁（xSetSystemCall 补 CreateFileW 等，见 PortSQLiteAppContainer）
- 不持久时自动退纯内存态（不崩）
- 挂起时显式落盘：`WebCoreFlushCookiesToDisk` + `WebCoreFlushStorage` + `WebCorePerfFlush`，deferral 由 UI 线程 2s 看门狗收尾（CompleteSuspendDeferral）
- **风险点**：SQLite App-Container VFS 补丁（真文件 vs 内存回退的切换）；挂起时序（三连落盘 vs 冻结）

### 2.4 渲染与合成
- 软件路径：Cairo paintToRGBA；GPU 路径：WebCoreComposite（flush→updateBacking→applyAnimations→beginPaint/paint/endPaint→eglSwapBuffers）；离屏 readback 验证
- **TileGrid v2**（ahorn42）：分块栅格 + DrawRecorder，模型/存储核在 `platform/graphics/texmap/TextureMapperTileGrid*`（SML 状态机，sml.hpp vendored）；present-only 修复、脏块升级上限、完成信号走 wk* 统计钩子
- 双击缩放/长按链接菜单/拖拽指针事件/轴锁滚动/嵌套滚动（WebCoreWheelAt）/合成 fixed-sticky
- 页面宽度因子（`WebCoreSetPageWidthFactor`）、viewport 可变（WebCoreResize 全有或全无）
- **风险点**：TileGrid 的 present 修复循环、脏块判定（空帧判定 vs 未画块）、EGL surface readback 可信度；两套 tiling（上游 TiledBackingStore 仍在编译但无人引用）；软件/GPU 切换路径

### 2.5 输入与手势
- 点击/双击（TapPolicy 链路 + WebCoreClickAtCount）、长按（250ms 地图/链接）、拖拽（WantsDragAt/DragAt 指针事件化）、滚轮/捏合（PinchSnap ±33%）、IME 文本注入（TypeText/KeyAction）、轴锁、触摸滚动滞后诊断
- **风险点**：手势状态机退出路径（每种手势唯一出口）；hit-test 在非 1:1 scale 下的修正；焦点元素跟随（旋转/键盘遮挡）

### 2.6 UI 宿主（harness）
- 地址栏/建议下拉/标签切换/抽屉/设置页/OOBE/动作卡/长按链接菜单/下载卡片/历史后退
- XAML codegen：Fallback（xamlgen 手搓，gen-xaml-codebehind.ps1 生成 .g.hpp，字段表在 MainPage.g.h）
- i18n（中英表）、实体返回键、状态栏/导航栏隐藏、内存压力联动（WebCoreReleaseMemory/SetMemoryPressure）
- **风险点**：Fallback codegen 与 XAML 的同步（本次补过 LinkMenu 5 字段、挂起处理器误留 deferral 行）；合并缝（M4 perf/0.2.0 storage 两套 UI 块在 MainPage.xaml.cpp 里并集过）

### 2.7 明确关闭/降级的能力
| 项 | 状态 |
|---|---|
| JIT / DFG / FTL / WASM | 关（LLInt） |
| 媒体播放（音视频） | 关（ENABLE_VIDEO=0；JSHTMLMediaElementCustom 整文件跳过） |
| WebGL | 关（相关 GL TU 从源列表剔除） |
| 远程 inspector | 关（LegacyWebSocketInspectorInstrumentation 全套 no-op stub） |
| 弹窗/模态/工具栏可见性 | ChromeClient 恒 false（2.54 已删这些虚，留的也都是 no-op） |
| WebTransport | 拒绝（promise reject） |
| RiceBackend | 拒绝（USE(LIBRICE) 关） |
| AX 无障碍 | 仅最小桩（stubs-ax）；焦点变更钩子签名已改 AccessibilityObject* |
| GetKeyState 系键盘态查询 | App Container 无此 API → 降级（修饰键态靠宿主随事件下发） |
| Gigacage | 系统分配器接管（webcore-driver-stubs：基址藏块布局，free 统一安全） |
| JIT 池统计 | LLInt 下打印 "(LLInt build, no JIT pool)" |

## 3. C ABI 接缝（73 个导出，harness↔driver）

定义在 `harness/WebCoreDriver.h`（与 `port/WebCoreDriver.h` 必须同步！）。分组：

- **导航/生命周期**：WebCoreLoadUrl / WebCoreSessionLoad / WebCoreCloseSession / WebCoreLiveTick / WebCoreSessionPaint / WebCoreResize / WebCorePreconnect / WebCoreDownload
- **呈现**：WebCoreGpuInit / WebCoreComposite / WebCoreCompositeReadback / WebCoreGpuLayerInfo / WebCoreGpuSetFlip / WebCoreEnableCompositing / WebCoreSetPresentRequestCallback / WebCorePresent*
- **输入**：WebCoreClickAt(Count) / WebCoreScrollBy / WebCoreWheelAt / WebCoreZoomWheelAt / WebCoreDragAt / WebCoreWantsDragAt / WebCoreLongPressAt / WebCoreTapPolicyAt / WebCoreTypeText / WebCoreKeyAction / WebCoreFocusedEditable / WebCoreRevealFocusedElement / WebCoreSetBottomOcclusion
- **查询**：WebCoreGetDiag / GetLastError / GetTitle / GetUrl / GetPageScale / GetScrollState / GetMemoryStats / GetPendingResourceCount / GetFrameHash / GetLink(Count) / WebCoreLinkAt / WebCoreSyncLinks / WebCoreIsScrollableAt
- **查找**：WebCoreFindString / FindNext / FindClear
- **配置**：WebCoreSetCACert(Path|Blob) / SetCookieJarPath / SetCookieJsonPath / SetProfilePath / SetPublicSuffixListBlob / SetUserAgentMobile|String / SetIPv4Only / SetSpeculativePrefetch / SetPageScale / SetPageWidthFactor / SetMemoryPressure / SetPerfLogPath / SetCrashLogPath / SetCookieJarPath
- **存储/诊断**：WebCoreFlushCookiesToDisk / FlushStorage / GetStorageDiag / EvalJS / EditDebug / ReleaseMemory / CrashNote / PerfFlush
- **UI 桥**：WebCoreTakeUIRequest / WebCoreCompleteFileChooser（异步 UI 请求队列，会话换代时作废）

**扫 bug 重点**：这个面的每条导出都同时被 C++/CX（异常开、不同 CRT 实例）与 clang-cl（异常关）调用；
参数都是 POD/指针/UTF-8 缓冲。经典坑：缓冲区长度约定、引擎线程亲和（所有调用必须串行到引擎线程）、
返回码语义（kOK/kErr*）、迟到回调（m_opSeq 看门狗）。

## 4. 平台约束（决定 bug 形态）

- **App Container**：无 GetKeyState/QueryMemoryResourceNotification/RegisterWaitForSingleObject/
  UnregisterWaitEx/psapi(旧)/desktop 特权 API；文件系统只能写 LocalState/临时目录；
  **App Container 无系统证书库**（HTTPS 依赖打包 cacert.pem 注入）。
- **单引擎线程铁律**：present 只在引擎线程；UI 线程绝不同步等引擎（ANGLE surface 创建/尺寸
  marshal 回 dispatcher，互等=死锁）。
- **内存**：3GB 设备有 OOM 前科（防 OOM：内存压力联动、后退页面缓存关、MemoryManager）。
- **崩溃可观测**：WER 对 fastfail 无 dump → crash.txt 三条腿（WTF hook/VEH/SIGABRT）+
  WTFCrashWithInfoImpl 本地定义（webcore-driver-stubs）写断言位置。

## 5. 2.54 重排的适配点（bug 扫描最高优先级）

这些是近一周改动、语义敏感、且多为"合并缝"：

| 区域 | 文件 | 性质 |
|---|---|---|
| Cookie SameSite 全链路 | platform/network/curl/{CookieUtil,CookieJarDB,NetworkStorageSessionCurl}、platform/Cookie.h | 0.2.0 补丁（WK_WINUWP 三处） |
| PSL 换方案 | stubs-other.cpp（blob 解析）vs PublicSuffixStorePort（休眠） | 合并决策点 |
| WebSocket 双实现 | PortWebSocket（用）/ PortSocketStreamHandle+PortStorage provider（休眠） | 合并决策点 |
| ImageFrameWorkQueue 串行化 | platform/graphics/ImageFrameWorkQueue.cpp（JSVALUE32_64 双字洞值、借 tag 寄存器） | 手写代码生成 |
| Timer/RenderText 尺寸哨兵 | platform/Timer.cpp、rendering/RenderText.cpp | ABI 尺寸假设 |
| CheckedRef 悬垂策略 | wtf/CheckedRef.h（报告后继续 vs 崩溃） | 行为变更 |
| FastMalloc/OSAllocator | wtf/FastMalloc.cpp、wtf/win/OSAllocatorWin.cpp（分段提交、MEM_RESET） | 内存语义 |
| bmalloc/JIT 化边界 | bmalloc.h(BPlatform 毒丸例外、aligned_alloc 适配)、VMAllocate.h | 分配器 |
| crash 链路 | wtf/Assertions.h（WTFCrashWithInfoImpl 3 参签名+本地定义） | 崩溃语义 |
| 挂起/UI 合并缝 | harness/MainPage.xaml.cpp（四块并集、deferral 行曾误留） | 合并缝 |
| 驱动 API 漂移适配 | port/{PortChromeClient,PortWebSocket,LoadingFrameLoaderClient,PortStorage,stubs-*}.cpp/h | 2.54 签名适配 |
| TileGrid v2 | texmap/TextureMapperTileGrid*、sml.hpp、DrawRecorder | 新子系统 |
| 生成器同步 | harness/xamlgen/MainPage.g.h（字段表）↔ MainPage.xaml ↔ .g.hpp | 三方一致性 |

## 6. 诊断与测试面（复现/验证用）

- **LocalState 侧信道**（WDP 拉取）：`crash.txt`（崩溃三腿）、`stage.txt`（阶段/手势/上下文菜单日志）、
  `perf.csv`（导航/渲染分相，perf.txt 开关）、`mem.txt`（内存快照）、`console.txt`（JS 控制台环）
- **测试资产**：`tests/web-platform/pages/`（cookie/SameSite/存储/iframe/websocket/CSP 等真机页）、
  `tests/psl/`（PSL 单测，x64 宿主可跑）、`tests/tilegrid/`（TileGrid v2 模型单测+回放）
- **真机部署**：`tools/deploy-launch.ps1`、`tools/Deploy-Robust.ps1`、`tools/auto-diag2.ps1`
- 复现构建四步：configure-gpu.ps1 → build-engine-gpu.ps1 → link-driver-gpu.ps1 → build-harness.ps1

## 7. 已知未竟项（不算 bug 但别当 bug 报）

- JIT 关（docs/JIT-254-PORTING.md；门控 `-DAPOTHEOSIS_JIT=ON` 可开但编译未过）
- WP8.1 移植未启动（docs/WP81-FEASIBILITY.md）
- dcce 构建机当时失联，本轮产物出在本机（dcce 环境已搭好可复用）
- `harness/WebCoreDriver.h` 与 `port/WebCoreDriver.h` 双副本需同步维护（历史遗留约定）
