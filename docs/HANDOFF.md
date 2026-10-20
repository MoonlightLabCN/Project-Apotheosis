# 0.2.5.x 交接单（UI 打磨 / JIT 实验开关 / 性能诊断 / 未决回归）

> 2026-10-07 写。覆盖 `0.2.4.4 → 0.2.5.8` 的全部改动与当前阻塞。
> 项目总体状态以 `PROJECT-OVERVIEW.md` 为准；JIT 历史见 `docs/JIT-HANDOFF.md`(09-27)
> 与 `docs/JIT-254-PORTING.md`(09-26)。本单是这三者的增量。

## 0. 一句话状态

- **最后真机验证稳定的版本：`0.2.4.4`（用户确认"没问题"）。**
- `0.2.5.8` 交互式打开任意网站闪退的**根因已定位并修好**（引擎侧，见 §10）：
  `ENABLE(JIT)` 编进引擎但运行时 `Options::useJIT()` 为 false 时，
  `VM::jitStubs` 从未创建，而 LLInt/host-function 路径照样要它 → 空指针
  （`JITThunks::hostFunctionStub`，this=nullptr）。0.2.4.4 是 `ENABLE_JIT 0`
  所以整条机器不存在。**§10 是当前工作单**，含第二个 bug（RunLoopGeneric
  的 `ScheduledTask` 生命周期）与驱动侧 JIT 护栏。
- 回归窗口 = `0.2.4.4 → 0.2.5.8` 的全部改动（清单 §2）；§4 的"第一刀"计划
  已被 §10 的实证取代（不需要改 `Options.h` 的默认值）。

## 1. 本会话已交付的内容

已被真机验证过的：

- **SQLite `GetFileSize` 空系统调用**（localStorage 一有内容启动必崩）：
  `port/PortSQLiteAppContainer.cpp` 新增 `apoGetFileSize`（`GetFileSizeEx` 实现），
  并加入 `kRequired` 门禁清单。0.2.2.0 起验证。
- **JIT 真正编进引擎**：此前所有出货构建都是纯 LLInt。改动见 §2C。
- **JIT 运行时开关**：ABI `WebCoreSetJitEnabled/WebCoreGetJitEnabled`；设置页
  EXPERIMENTAL 区；**0.2.5.7 起默认关**（0.2.5.6 默认开时在 github.com 崩过，
  见 §6）。
- **JIT 池诊断**：`stage.txt` 的 `jit-diag` 行（useJIT / canUseJIT / 池区间 /
  一次试探分配）；`ExecutableAllocator.cpp` 的 WK_WINUWP 回调
  `wkWinUWPSetJitPoolDiagSink`（池为空时打印原因）。
- **性能诊断记录开关**（设置 → DEVELOPER）：开 = 引擎 `perf.csv` 逐操作计时 +
  每次导航一行 `browse-log.txt`（`WebCorePerfLastNav` 取最近 nav 行）。
- **输入延迟**：页面级禁缩放（`WebCoreTapPolicyAt` reason 3/4）时，单击不再等
  300ms 双击窗口（`m_dtapPageNoZoom`，导航复位）。
- **动画帧率**：`ScheduleWakeComposite` 两个旧上限放宽——慢帧占空比上限
  200→50ms、失控动画 1000→250ms。
- **渲染清晰度设置**（设置 → DISPLAY）：标准 720p / 高清 1080p / 原生分辨率；
  `ClarityShortSideTarget()` + `ClarityFactorRatio()` 同比例补偿页面宽度倍率，
  避免改清晰度把排版拉成桌面宽；首帧锁定，**重启生效**。
- **字体**：fontconfig 统一 `hintslight` + 灰阶抗锯齿。
- **visualViewport API 打开**（CF 人机验证需要）：`setVisualViewportAPIEnabled(true)`。
  **是否解决循环验证未验证。**
- **WDDM 1.1/1.2 老驱动降级**：`PlatformDisplayWin` 的"带属性 → 无属性 → NULL 后端"
  阶梯（**属性已撤回，现在第一刀仍是空属性**）+ `harness/GpuInitGuard.cpp`(SEH 壳)。
- 崩溃取证：`browse-log.txt` 已加入设置页"导出调试日志"清单。

## 2. 0.2.4.4 之后的全部改动（二分用）

### A. harness（C++/CX 宿主 + XAML）
- `harness/MainPage.xaml` / `MainPage.xaml.cpp` / `MainPage.xaml.h`
  - 设置页：EXPERIMENTAL 区（JIT 开关）、渲染清晰度 ComboBox、性能诊断 ToggleSwitch、
    小节样式统一（`SetSection`/`SetHint`）
  - 帧率上限（`ScheduleWakeComposite`）、单击免等（`m_dtapPageNoZoom`）、
    清晰度（`g_clarityEffective`/`ClarityShortSideTarget`/`ClarityFactorRatio`）
  - 诊断：`ApplyPerfLogSetting` / `LogBrowsePerf`（OnNavDone 里调用）
  - `GpuInitGuarded(...)` 取代直接调 `WebCoreGpuInit`（两处）
  - fontconfig 规则：`SetupRuntimeEnv()` 里新增 hintstyle/antialias match
- `harness/GpuInitGuard.cpp` / `.h`：**新文件**，MSVC 单独编译（`CompileAsWinRT=false`），
  就是 `__try/__except` 包 `WebCoreGpuInit`，异常→`-23`
- `harness/Harness.vcxproj`：加入 GpuInitGuard
- `harness/xamlgen/MainPage.g.h`：新增控件字段（fallback XAML 工具链的字段表是手维护的！）
- `harness/Package.appxmanifest`：版本 → `0.2.5.8`
- `harness/WebCoreDriver.h`：ABI 同步（与 `port/WebCoreDriver.h` 必须同改）

### B. port（驱动）
- `port/WebCoreDriver.cpp` / `.h`
  - `WebCoreSetJitEnabled` / `WebCoreGetJitEnabled`（池无效时拒绝打开）
  - `WebCorePerfLastNav`（人读性能摘要）
  - `jitPoolDiagLine()` / `jitPoolSinkWrite()` 与 `wkWinUWPSetJitPoolDiagSink` 注入
  - `page->settings().setVisualViewportAPIEnabled(true)`（三处建会话点）
- `port/PortSQLiteAppContainer.cpp`：`GetFileSize`
- `port/configure-gpu.ps1`：`-DAPOTHEOSIS_JIT=ON`（JIT 门）
- `port/compile-driver-gpu.ps1`：加 `-DWK_WINUWP_JIT=1`

### C. WebKit 引擎（`WebKit/` 未进 git；全部 `#if defined(WK_WINUWP)` + `Apotheosis:`）
1. `Source/JavaScriptCore/runtime/Options.h` —— `jitEnabledByDefault()` 在 WK_WINUWP
   返回 `true`。**这是"JIT 从未启用"的根因**：上游默认 `isAddress64Bit()`，ARM32 恒 false，
   于是 `computeCanUseJIT` 直接否掉、池根本不建。**（0.2.5.6 起生效；也是 §4 第一刀的靶子。）**
2. `Source/JavaScriptCore/jit/JITInlineCacheGenerator.h` —— 无 DFG 时把 DFG 类型从
   `CompileTimePropertyInlineCache` variant 里去掉（MSVC `std::visit` 要求覆盖所有分支）
3. `Source/JavaScriptCore/runtime/ScriptExecutable.cpp` —— 补 `CompilationResult.h`
4. `Source/JavaScriptCore/bytecode/InlineCacheCompiler.cpp` —— 32 位补
   `getByIdMegamorphicGetterHandler` 空 stub（`{ return { }; }`，与既有 32 位 stub 列表同风格）
   + 内联 megamorphic getter 发射点 `USE(JSVALUE64)` 守卫
5. `Source/JavaScriptCore/jit/ExecutableAllocator.cpp` —— 池诊断 sink + 两处回调
6. `Source/WebCore/platform/graphics/win/PlatformDisplayWin.cpp` —— EGL display 阶梯；
   **注意**：曾加过 `ENABLE_AUTOMATIC_TRIM` / `MAX_VERSION 9_3` 属性，导致 0.2.5.7
   "任意网站一开就崩"（预编译 libEGL 不认识的属性会把进程带走），**已撤回**，别再往回加。
## 3. 0.2.5.8 的崩溃证据（用户报告 + crash.txt）

```
==== crash 2026-10-07 16:17:04.542 tid=3368 ====
reason: SEH code=0xc0000005 address=0x02b1f115 access=0 fault=0x00000044
module Harness.exe base=0x00ba0000 size=0x030f9000
context: pc=0x02b1f115 lr=0x0262caed sp=0x1b3fd0e8 r0=0x00000040 r1=0x1b3fd118
```

- `lr` 符号化 → `JSC::VM::getCTIStub(CommonJITThunkID)`；cand 帧里还有
  `JITThunks::ctiStub`、`llint_entry`、`operationDefaultCall` 以及若干
  BytecodeGenerator 的 emit 函数。**cand 是栈扫描的启发式结果，不可当栈用**，
  只能当线索。
- 同一次启动的 `stage.txt`：`jit-diag useJIT=0 canUseJIT=1 pool=[1C410000,1E410000)`
  ——**JIT 关着，但池已建、canUseJIT=1**（0.2.5.6 起 Options.h 的副作用）。
  0.2.4.4 时是 `canUseJIT=0`、池不存在。
- 没有 WDP minidump（crashcontrol 是事后才开）。**0.2.5.8 的 crashcontrol 已打开**
  （`POST /api/debug/dump/usermode/crashcontrol?packageFullName=<FN>`），再崩一次
  就能拿到全栈；`crash\watch-0258.ps1` 就是自动拉 dump 的 watcher。
- 用户补充：**0.2.4.4 完全没问题**。

## 4. 建议的定位顺序

1. **第一刀（最便宜、信号最强）**：把 `Options.h` 的 `jitEnabledByDefault()`
   WK_WINUWP 分支改回 `isAddress64Bit()`，其余 0.2.5.8 改动全留。效果 =
   回到 0.2.4.4 的引擎初始化状态（不建池、canUseJIT=0），但保留全部 UI/诊断改动。
   - 只改一个头 → `ninja -j4` 重编引擎（约 40min）→ 重链驱动 → 重打 harness。
   - 不崩 → 回归在"JIT 初始化路径"（池存在 / `populateJITOperations` / Options 生效
     时机），与 UI 无关；继续在 §2C 的 6 个改动里二分。
   - 还崩 → 回归在 harness 侧或 visualViewport/PlatformDisplayWin。
2. 若第一刀没好：**交叉组合**——用 0.2.4.4 的 `Harness.exe` 配 0.2.5.8 的
   driver/引擎，再反过来。这一步能一刀切开 harness 侧还是引擎侧（ABI 两份头必须同版）。
3. 最可疑的三项，按剥离顺序：
   1. `setVisualViewportAPIEnabled(true)`（全新的引擎事件路径，滚动/缩放都触发）
   2. `PlatformDisplayWin` 的 EGL 阶梯（虽然属性已撤，阶梯本身还是新代码）
   3. `GpuInitGuard` SEH 壳（新文件、新编译单元）
4. 拿到 dump 后：`tools\Symbolize-Crash.ps1 -Dump <file>` 或
   `cdb -z <dump> -y <harness ARM Release 目录> -c ".ecxr; kb 30; q"`。
   注意 cdb 的模板名符号很慢，用地址形式 `u <addr>` / `kb`。

## 5. 取证基础设施（本会话全部实测可用）

- **拉文件**（读 LocalState）：
  `pwsh -File tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.5.8 -File stage.txt`
- **写文件**（WDP 只认 multipart POST，PUT/裸 body 会 400/404）：
  `POST /api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=<FN>&path=%5CLocalState&filename=<name>`
  + `MultipartFormDataContent`（`ContentDisposition.Name='"file"'`，
  `FileName='"<name>"'`）。
- **自动跑站点**：写 `LocalState\autodiag.txt`（每行一个 URL，`DESKTOP:` 前缀 =
  桌面 UA）→ **先杀掉 app 再冷启动**（只"启动"已挂起的实例不会重跑 autodiag）→
  轮询 `autodump.txt`（每站一段 `########## URL:`）。
  **注意：autodiag 走离屏 GPU（`WebCoreGpuInit(offscreen)`），与交互式的
  SwapChainPanel 路径不同，交互态崩溃未必能复现。**
- **引擎性能日志**：放 `LocalState\perf.txt`，或开设置里的"性能诊断记录" →
  `perf.csv`（逐操作阶段耗时）+ `browse-log.txt`（每次导航一行 + 人读摘要）。
- **dump 收集**：
  - 开：`POST /api/debug/dump/usermode/crashcontrol?packageFullName=<FN>`
  - 列：`GET /api/debug/dump/usermode/dumps`
  - 下：`GET /api/debug/dump/usermode/crashdump?packageFullName=<FN>&fileName=<name>`
- **app 内兜底记录**：`LocalState\crash.txt`（SEH / signal / RELEASE_ASSERT /
  WTF checked-ptr 都会写一条，带 RVA 帧）。
- 进程控制：`DELETE /api/taskmanager/app?package=<base64(FN)>` 杀；
  `POST /api/taskmanager/app?appid=<b64(AUMID)>&package=<b64(FN)>` 启动。
## 6. 已知网站问题（附诊断结论）

- **抖音电脑版白屏**（根因已定）：`console.txt`
  `TypeError: e.play is not a function` / `e.pause is not a function`。
  本构建 `ENABLE_VIDEO=0`（`build-clang-gpu/cmakeconfig.h`），HTMLMediaElement
  整类不存在；抖音的 React 包在初始化时调 `video.play()` 直接抛异常，整个 app 初始化
  中断 → 白屏（diag 里 `els=6 txt=0`、只有 1 个资源）。
  **修法**：`ENABLE_VIDEO=1` + 一个空 `MediaPlayerPrivateInterface`（元素在、
  `play()` 可调、不出声/不出画）。工程量大，单独一轮做。
- **Cloudflare 人机验证循环**：`nowsecure.nl` 上 `ReferenceError: Can't find
  variable: visualViewport`；`visualViewportAPIEnabled` 在本构建默认 **false**，
  已由驱动显式打开（0.2.5.5+）。**是否解决未验证**（等 0.2.5.x 回归修好后再试）。
  若仍循环，下一层原因是 TLS 指纹（curl/OpenSSL 的 JA3 与真浏览器不同）。
- **deepseek 手机版登录卡住 / 桌面版发不出消息**：页面本身加载正常（sign_in
  113 元素、JS 正常、KaTeX 等资源都到）。**需要登录态的日志**才能继续：
  开"性能诊断记录"→ 登录 → 发一条消息 → 取 `browse-log.txt` + `console.txt`。
  桌面版二维码/微信通道本身工作正常（`wxLogin.js`）；console 里
  `localhost.weixin.qq.com:13xxx/api/check-login` 的 CORS 报错是"探测本机微信
  客户端快捷登录"的正常噪音，不是故障。
- **Lumia 930 GPU（GitHub issue #6）**：已加 ANGLE 降级阶梯 + SEH 壳（见 §1）。
  issue 里第二条诉求"930 上 720p 拉伸发虚"——本会话的"渲染清晰度 → 原生分辨率"
  正好对上，可回复用户试。

## 7. 环境 / 工具坑（本会话踩过，照做省时间）

- **MSBuild `CL.exe 命令行开关无效`（System.ArgumentException：
  键"NO_PROXY"冲突）**：环境里同时存在大写/小写重复的代理变量（`NO_PROXY`/`no_proxy`、
  `HTTPS_PROXY`/`https_proxy`…），MSBuild CL 任务构造环境字典时炸。构建前清掉：
  `Get-ChildItem Env: | Where-Object { $_.Name -match '(?i)proxy' } | Remove-Item`。
- **命令封装器会把多行字符串写成 CRLF**：对 CRLF 文件做多行 `Replace` 时，pattern
  要先归一化成 CRLF（本会话的 `Rep` 写法：`($a -replace "\r\n","\n") -replace "\n","\r\n"`）；
  反过来对 LF 文件要先剥 `\r`。XAML 是 CRLF，`port/*.cpp` 多为 LF。
- **构建并行度**：`ninja -j4`（12 核 16G 机 `-j12` 编 WebCore 大 unity TU 会 OOM）。
  构建前把进程设 `BelowNormal`（子进程继承优先级），别抢用户前台。
- **设备 Wi-Fi 极不稳定**：上传经常半路断（`Error while copying content to a stream`）。
  `deploy-launch.ps1` 是"先卸载、再上传"，半路断会留下**未安装**状态。
  用带重试的 watcher（本会话 `_deploy-*.ps1` 模板：循环 ping → 部署 → 检查
  `已安装.*<Ver>` → 成功才退出）。
- **写 WDP 文件必须 multipart POST**；`pull-file.ps1` / `deploy-launch.ps1` 里的
  CSRF 处理（`X-CSRF-Token` + CookieContainer）要照抄。
- 部署成功 `deploy-launch.ps1` 会直接启动 app。
- **回滚**：`harness\AppPackages\Harness\` 现在只有 `0.2.5.1 … 0.2.5.8` 的包，
  **0.2.4.4 的 appx 已不在**；要回到 0.2.4.4 需要按 §2 反向剥离源码重建
  （或用 git 上 `0.2.4.4` 对应修订重编）。

## 10. 0.2.5.9/10：0.2.5.8 交互闪退的根因已定（2026-10-07 晚）

### 10.1 根因（minidump 实证，别再重查）

0.2.5.8 的崩溃链（`crash\Harness.exe.6296.dmp` 已符号化）：

```
WebEngine::loop → WebCoreSessionLoad → RunLoop → CurlDidReceiveData → …
→ ScriptElement::executeClassicScript → ScriptController::evaluateInWorld
→ JSWindowProxy::create → JSDOMWindow::finishCreation
→ JSGlobalObject::finishCreation/init → FunctionPrototype::addFunctionProperties
→ JSObject::putDirectNativeFunctionWithoutTransition → JSFunction::create
→ VM::getHostFunction → JITThunks::hostFunctionStub        ← this == nullptr
→ HashTable<Weak<NativeExecutable>>::find  ← ldr r7,[r0]，r0 = 0x3C0
```

`JITThunks` 对象内 `m_hostFunctionStubs` 偏移 **0x3C0**、`m_commonThunks` 偏移
**0x44**——两者与早前 crash.txt 记录的读故障地址一致 → 结论：
**`VM::jitStubs == nullptr` 却被解引用**。

成因：`VM::VM`（`runtime/VM.cpp`）只在 `if (Options::useJIT())` 时创建
`jitStubs`；而 `JITThunks` 的**所有消费者都不看 useJIT**——
`VM::getCTIStub()`（844-849 行）根本不带 `if (Options::useJIT())` 守卫，
`VM::getHostFunction()` 的守卫是针对**整个分流**的，`InlineCacheCompiler::
emitExplicitExceptionHandler()` 也直接调 `getCTIStub`。于是：

- harness 启动时 `ApplyJitSetting()` 往引擎线程 post `WebCoreSetJitEnabled(0)`
  （`settings.ini` 里 `jit=` 的持久值）；
- 这个 post 与首个 Page/VM 的创建**竞态**：先建 VM（useJIT=0 → 不建 jitStubs）
  后落 `useJIT=1`，或反过来 VM 建在 useJIT=0 而后续路径要 thunks；
- 0.2.4.4 是 `APOTHEOSIS_JIT` OFF ⇒ `ENABLE_JIT 0`，整条机器根本不存在 → 无回归。

### 10.2 已落的修复（0.2.5.9，WK_WINUWP 全部守卫）

1. **引擎：`Source/JavaScriptCore/runtime/VM.cpp`**
   `#if ENABLE(JIT)` 里 `VM::VM` 创建 jitStubs 的条件改为
   `Options::useJIT() || ExecutableAllocator::singleton().isValid()`。
   语义：**只要可执行池可用，JITThunks 就要建**（它不是"JIT 执行期"专属：
   LLInt 的 IC/异常 thunk、host function 桩都在里面）。池无效时仍跳过，
   保住 0.2.4.3 的教训（`JITThunks::initialize` 会在空 code 指针上 patch）。
2. **引擎：`Source/WTF/wtf/runloop 侧`（`generic/RunLoopGeneric.cpp` + `RunLoop.h`）**
   第二个 bug：`RunLoop::TimerBase::ScheduledTask` 被 free 时仍挂在
   `m_schedules` 红黑树里 → 树中 `CheckedPtr`（`m_root`/`m_left`/`m_right`）
   变悬垂 → 下一次 `TimerBase::start()`→`RedBlackTree::insert()` 释放该 CheckedPtr
   → crash.txt 记两条 note 后 **SIGABRT**。真机复现证据（jit=0 与 jit=1 都会）：
   ```
   checked-ptr: ScheduledTask deleted with 1 CheckedPtr(s) outstanding (zeroed and leaked)
   checked-ptr: released a CheckedPtr to an already-deleted object
   signal 22 (SIGABRT)  ws=232M commit=152M availPhys=1471M
   ```
   修法：`~TimerBase()` 里补 `RunLoop::detachScheduledTaskWithLock()`——按真实
   成员关系（而不是 `isScheduled()` 旗标的副本）走一遍小尺寸的 `m_schedules`，
   发现节点仍在链上就摘除。触发时写一条 stage.txt 行
   `jit: … ScheduledTask(still linked in m_schedules; unlinked by ~TimerBase)`
   不算 crash，但留下线索。
3. **驱动：`port/WebCoreDriver.cpp`**
   `WebCoreSetJitEnabled()`：`markJitSessionStarted()` 在 3 处 session 创建点置位
   `g_jitRefuseAfterSession`；已建过 session 后再来 `enable=1` **拒绝**
   （只降不升），并写 stage.txt `jit: late enable refused (session already exists)`。
   原因：迟到的 false→true 翻转会启动 baseline JIT tier-up，而该路径在 ARM32
   未验证，真机上表现为 pc=0x00000100/0x00040000（执行违例，lr 落在 JIT 池内）。

### 10.3 真机验证结果（0.2.5.9，Lumia 950）

- **主回归已消**：`stage.txt` 出现
  `jit-diag useJIT=0 canUseJIT=1 pool=[…]` + `after-load … rc=0`，
  JS 页面真渲染（`diag:` 行 `js=1/1 scripts=N`、`spa=[eval-ok rootCh=0]`）。
  长交互会话里连续加载 pigai.shop / deepseek.com / chat.deepseek.com 全 rc=0。
- RunLoop 修复与驱动 JIT 护栏的**完整验证回合还没跑**（0.2.5.10 + `-j4` 重建中，
  见 §10.4）。
- `settings.ini` 的 `jit=` 目前是 **0**（WDP multipart 写入，脚本
  `_setjit0.ps1`）；`jitresult.txt` 仍是 RW→RX 成功（JIT 原语可用）。

### 10.4 下一步（接手从这里开始）

1. `_rebuild-0260.ps1`（`ninja -j4` + `link-driver-gpu.ps1`）→ 成功后
   `_build-harness-0259.ps1` → `_deploy-0259.ps1`（版本 `0.2.5.10`）。
2. 复现/验收：`_verify-0259.ps1 -Ip 192.168.3.159 -Ver 0.2.5.10 -WaitSec 180`
   （冷启动 → 主页 → 页面加载，检查 crash.txt 是否新增记录）。
3. 若再崩：先看 §10.2 的第 2 条 note 是否出现（RunLoop 痕迹），
   再看 `reason: signal 22` 的 RVA（`cdb`/`llvm-symbolizer`，注意 `-j4`）。
4. 交互验收要点：连续导航 3~5 个真实站 + 滚动/捏合/双击缩放 + 地址栏回车，
   `crash.txt` 无新记录才算过。



## 8. 清理与遗留

- 仓库根有本会话产生的**未跟踪临时脚本**：`_build-025*.ps1`、`_deploy-025*.ps1`、
  `_watch-0258.ps1`、`_repro-github.ps1`、`_pull-0254*.ps1`、`_jit-*.ps1` 等。
  其中 `_watch-0258.ps1` **可能仍在后台运行**（轮询拉 0.2.5.8 的 dump）。
  交接时可删；要保留就留 `_watch-0258.ps1` 和 `_deploy-*.ps1` 模板。
- `harness/GpuInitGuard.cpp/.h` 是新文件，已加入 `Harness.vcxproj`。
- WebKit 树的改动**没有**重新生成 `wk-winuwp.patch`（本会话新增 6 个文件的改动，
  见 §2C），`WK_WINUWP-PATCH.md` 也未同步。有 pristine 基线时一并重生成。
- 未进 git 的还有 `port/_driver_compile_gpu.bat`（工具生成物）与若干构建日志
  （`build-025*.log`、`deploy-*.log`），按仓库习惯可留可删。

## 9. 本会话已确认的事实（别重查）

- `jit-diag` 行的含义：`useJIT=` `Options::useJIT()`、`canUseJIT=` 初始化时的判定、
  `pool=[起,止)` 可执行池区间、`probe256=` 一次 256B 试探分配。
  0.2.5.6+ 的正常形态是 `canUseJIT=1` 且池非空。
- `GetFileSize` 是 SQLite win32 VFS 表里第 23 项，在 `WINAPI_FAMILY_APP` 下为空指针
  （`os_win.c` 的 aSyscall 表；同一张表里 `CreateFileW`/`CreateFileMappingW`/
  `MapViewOfFile` 是更早修的）。
- `Options::useJIT` 的默认值来自 `Options.h::jitEnabledByDefault()`（上游
  `isAddress64Bit()`，ARM32 恒 false）；`PlatformEnable.h` 的 `WK_WINUWP_JIT` 门
  （`APOTHEOSIS_JIT`）只决定 `ENABLE_JIT` **编译开关**，不决定运行时默认值——
  两者都要开 JIT 才真正跑起来。
- `ENABLE_VIDEO=0`（`build-clang-gpu/cmakeconfig.h`）→ `HTMLMediaElement` 不存在，
  这是抖音白屏的唯一根因（不是 JIT、不是 GPU）。
