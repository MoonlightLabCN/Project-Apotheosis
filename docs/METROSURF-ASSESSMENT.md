# MetroSurf 完整度评估（对照 Apotheosis）

**评估日期**：2026-10-21
**上游源**：`https://github.com/MetroSurf/MetroSurf`（`main`，单提交 `ca30d0d` "MetroSurf 2: WebKit source"，2026-10-18 建仓）
**Fork**：`https://github.com/Jimmyxiao2009/MetroSurf`（已 fork）
**本地克隆**：`E:\Apotheosis\_metrosurf`

## 0. 它是什么

MetroSurf 自称 *"a powerful browser for Windows Phone and Windows Mobile"*。仓库**只放补丁内容**（2 MB，不含上游树），`about.txt` 写明：

> These files replace or add to WebKit at tag **webkitgtk-2.54.1**
> (commit `628d6ce1850e17225185a5807a9d0aa375bae2be`).
> `webkit/` mirrors the WebKit tree. `webkit-open/` is code from WebKit built into webkit.dll.

结构与我们的做法**同构**（补丁 + 宿主），但注意两点差异：

| 维度 | MetroSurf | Apotheosis |
|---|---|---|
| 上游基线 | **webkitgtk-2.54.1** | webkitgtk-2.52.4 |
| 组织形式 | `webkit/` 镜像树 + `webkit-open/`（打进 dll 的源码） | `patches/*.patch` + `port/` 驱动层 |
| 宿主 | `webkit.dll`（**进程内 dll**，非 UWP appx 壳？） | UWP appx（C++/CX）+ `WebCoreDriver-gpu.lib` |
| 定位 | 完整浏览器 | 引擎组件（宿主是独立壳，正在演进 UI） |

**关键判断**：它是**同一个上游的邻近版本移植**，因此**补丁大概率可直接借鉴**——这正是吸纳价值最高的形态。

## 1. 它有什么、我们缺什么（吸纳候选）

### 1.1 ★★★ 整块缺失的功能（`webkit-open/`，共 54 文件 / ~430 KB）

这些是**我们完全没有实现**的模块，且都是真实站点刚需：

| 模块 | 文件 | 体量 | 价值 |
|---|---|---|---|
| **WebSockets** | `WPWebSocketTask.cpp/h`、`WPWebSocketChannel.cpp/h` | 4 文件 44 KB | 高：现代站点广泛依赖ws/wss |
| **Storage** | `StorageAreaImpl`、`StorageTracker`、`StorageNamespaceImpl`、`StorageSyncManager`、`StorageThread`、`WebStorageNamespaceProvider`、`WebDatabaseProvider` | 19 文件 129 KB | 高：localStorage/sessionStorage 持久化 |
| **CacheStorage（Cache API）** | `CacheStorageCache/Manager/DiskStore/MemoryStore/Record/Registry`、`NetworkCacheKey/Coders` | 19 文件 113 KB | 高：Service Worker 前置 |
| **IndexedDB** | `Storage/InProcessIDBServer.cpp/h` | 2 文件 32 KB | 中高：越来越多站点用 IDB |
| **合成器场景态** | `compositor/CoordinatedSceneState.cpp/h` | 2 文件 10 KB | 中：若走 coordinated graphics 路 |
| **网络调度** | `WebResourceLoadScheduler.cpp/h` + `Errors.cpp` | 3 文件 28 KB | 中：资源加载优先级 |
| **历史记录** | `History/WPBackForwardList.cpp/h` | 2 文件 6 KB | 中：会话内前进后退 |
| **Ping** | `PingHandle.h`、`WPOpenHooks.cpp/h` | 3 文件 10 KB | 低：`<a ping>` |

> 注意：我们**已有** `WebStorage` 相关的部分实现（`port/PortSQLiteAppContainer.cpp` 里有 SQLite 适配），但**没有** `StorageTracker`/`StorageNamespaceProvider` 这套完整接法，也没有 CacheStorage / WebSockets / IDB。

### 1.2 ★★ `webkit/Source` 下的 122 个补丁文件（按领域）

| 领域 | 文件数 | 说明 |
|---|---|---|
| `WebCore/platform` | **44** | 最大块：texmap（12）、network/curl（10）、graphics（9）、win（3） |
| `WTF/wtf` | 11 | 含 `simde/arm/neon.h`、`MemoryFootprintWin`、`RAMSize`、`ThreadingWin` |
| `WebCore/page` | 9 | 含 **PointerEvent / PointerCaptureController**（触摸→指针事件）、`DOMTimer`、`Navigator` |
| `JavaScriptCore/runtime` | 8 | `Options.h`/`OptionsList.h`（JIT 开关）、`MicrotaskQueue`、`MachineContext` |
| `JavaScriptCore/dfg` | 6 | DFG JIT ARMv7 |
| `JavaScriptCore/jit` | 5 | `BaselineJITRegisters.h`、`AssemblyHelpers`、`JITInlineCacheGenerator` |
| `JavaScriptCore/assembler` | 2 | **`ARMv7Assembler.h`、`MacroAssemblerARMv7.cpp`** |
| `JavaScriptCore/bytecode` | 2 | `InlineCacheCompiler.cpp/h` |
| `JavaScriptCore/heap` | 2 | `Heap.cpp`、`MarkedBlock.cpp` |
| `bmalloc` | 6 | **mimalloc 替换/接入**（`prim/windows/prim.c`、`options.c`、`VMAllocate.h`） |
| `ThirdParty/skia` | 3 | `SkFontMgr_win_dw.cpp`（字体管理） |
| `WebCore/Modules` | 2 | **`MediaSource.cpp`**、`PermissionStatus.cpp` |
| `WebCore/dom` | 3 | `Document`、`MessagePort`、`PointerEvent.h` |
| `WebCore/loader` | 2 | `EmptyClients`、`EmptyFrameLoaderClient` |
| 其他 | 各 1 | `RenderText`、`SVGPathBlender`、`CSSPlatformColorResolutionState`、`PlatformScreenWin`、`CookieJarDB`、`ScrollingTreeCoordinated` |

### 1.3 ★★★ 与当前开放问题直接相关

**`JavaScriptCore` 的整条 ARMv7 JIT 补丁线**（assembler/dfg/jit/bytecode/heap，共 25 文件）——我们刚在 JIT 上抓到空指针崩溃（已定位为 `AudioLog` 的 null 解引用，与 JIT 无因果），但**我们的 JIT 是否真正可用**仍是未验证项。MetroSurf 的这条线是**同代上游、同架构（ARMv7/Thumb2）的现成参考**，对验证/对齐我们的 `WK_WINUWP` JIT 补丁有直接价值。

特别值得看的：
- `WTF/wtf/simde/arm/neon.h` —— SIMD 的 ARM NEON 适配（我们可能缺）
- `bmalloc` 接入 **mimalloc** —— 内存分配器替换，对 2GB 内存的 Lumia 是实打实的收益
- `WTF/wtf/win/MemoryFootprintWin.cpp` + `RAMSize.cpp` + `AvailableMemory` —— 内存压力处理（我们 OOM 踩过坑）
- `WebCore/page/PointerCaptureController` + `PointerEvent` —— 触摸/指针事件完整度（影响交互保真）
- `WebCore/platform/graphics/texmap/*`（12 文件）—— 我们的 GPU 合成线可对照
- `WebCore/Modules/mediasource/MediaSource.cpp` + `SourceBufferPrivate.cpp`（64 KB + 93 KB）—— **MSE**，我们明确"未开始"的一项

## 2. 完整度结论

| 能力 | Apotheosis | MetroSurf | 判断 |
|---|---|---|---|
| 引擎移植（ARM32/UWP） | ✅ 真机跑通（画面+声音） | 自述完整浏览器，**未在真机独立验证** | 我们领先（有真机证据） |
| Media Foundation 播放 | ✅ H.264 软解 + 音频（真机确认） | 有 `MediaPlayer.cpp/h`、`SourceBufferPrivate` 改动 | 我们音频线已闭环 |
| **MSE** | ⛔ 未开始 | ⚠️ 有改动（需细看是否可用） | **它可能领先** |
| **WebSockets** | ⛔ 无 | ✅ 完整实现 44 KB | **它领先** |
| **Storage 持久化** | ⚠️ 部分（SQLite 适配） | ✅ 完整 129 KB | **它领先** |
| **CacheStorage / IDB** | ⛔ 无 | ✅ 完整 113 KB + 32 KB | **它领先** |
| GPU 合成 | ✅ ANGLE/TextureMapper 真机跑通 | 有 texmap 补丁（12 文件） | 各有侧重，可对照 |
| JIT | ⚠️ 构建启用但未验证可用性 | ✅ 完整 ARMv7 JIT 补丁线（25 文件） | **它有参考价值** |
| 内存管理 | ⚠️ 踩过 OOM | ✅ mimalloc + 内存压力 | **它领先** |

## 3. 吸纳建议（按性价比排序）

1. **WebSockets 4 文件** —— 体量小、边界清晰、无深层耦合，最易移植。
2. **内存管理三件套**（`MemoryFootprintWin` + `RAMSize` + `AvailableMemory` + mimalloc）—— 解决我们的 OOM 痛点，风险可控。
3. **`Page/PointerCaptureController` + `PointerEvent`** —— 提升交互完整度，与现有触摸手势层衔接。
4. **Storage 完整接法** —— 在现有 SQLite 适配之上补齐 `StorageTracker`/`NamespaceProvider`。
5. **CacheStorage / IDB** —— 体量大但边界清晰，可整目录搬。
6. **JIT ARMv7 补丁线** —— 作为对照，用于验证我们的 JIT 是否完整；**不要整体覆盖**（上游版本不同，2.54.1 vs 2.52.4）。
7. **MSE** —— 需要先确认它的实现完整度（`SourceBufferPrivate.cpp` 有 93 KB，可能确实可跑）。

## 4. 吸纳前提与风险

- ⚠️ **上游版本差**：2.54.1 vs 我们的 2.52.4。补丁不能直接 `git apply`，需要按文件人工对齐（API 可能已变动）。
- ⚠️ **它的宿主形态未知**：`webkit-open/` 说是"built into webkit.dll"，看起来是**非 UWP 的进程内 dll**，而我们是 UWP App Container。触摸/WinRT 相关的补丁（如 WebSockets 用 `Windows::Networking::Sockets`）需确认在 App Container 下可用。
- ⚠️ **未验证**：仓库只有 1 个提交、创建于 2026-10-18、无 README/issue/release。它是**源码快照，不是经过验证的发布**。吸纳前应逐个模块在真机验证。
- ✅ **同构优势**：它也是补丁式移植，`webkit-open/` 的模块边界清晰，便于裁剪后并入我们的 `port/`。

## 5. 下一步（未开始）

- [ ] 精读 `webkit-open/WebSockets/`（4 文件）→ 评估直接移植
- [ ] 精读 `bmalloc` mimalloc 接入 → 评估替换我们现在的分配器
- [ ] 精读 `MemoryFootprintWin`/`RAMSize`/`AvailableMemory` → 对齐内存压力处理
- [ ] 核对 `MediaSource.cpp`/`SourceBufferPrivate.cpp` 的 MSE 完整度
- [ ] 对照 JSC ARMv7 JIT 补丁线与我们的 `WK_WINUWP` JIT 改动差异
- [ ] 确认它的宿主形态（dll vs appx）与 WinRT API 在 App Container 的可用性
