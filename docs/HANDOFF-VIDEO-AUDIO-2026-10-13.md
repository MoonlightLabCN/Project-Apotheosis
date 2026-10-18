# 交接：`<video>` / `<audio>` 线（media-mf）

**日期**：2026-10-13
**分支**：`media-mf`（HEAD = `c99c557`）
**上游 WebKit 仓库**：`WebKit/`（gitignored，独立 git repo，分支 `wk-tnew`，全部 port 改动未提交，其中 13 个 `UU` unmerged 条目）
**真源码在哪**：`WebKit/` 下的改动是**易失的**（不在仓库、未提交）。真正入库的是 `patches/2026-10-09-winuwp-media-mf.patch`（当前 128 KB / 7 文件，反向自检 `git apply --check -R` 通过）。改完 `WebKit/` 必须重新生成补丁再提交，否则工作会丢。

---

## 0. 一句话现状

**"没有画面"的根因已经找到并修好，但修复尚未在真机验证。** 阻塞不在代码，在设备：手机息屏后 WiFi 断开（`192.168.3.159` 一度从 ARP 表消失），导致 0.2.5.29 装不上去。

- **0.2.5.29 已构建**（`harness/AppPackages/Harness/Harness_0.2.5.29_ARM_Test/Harness_0.2.5.29_ARM.appx`，54 MB，07:51:37），**未安装、未验证**。
- 设备上最后一次成功安装的是 **0.2.5.27**（`IsInWindow` 修复；那个版本修掉了我自己引入的 0.2.5.26 崩溃，页面能正常加载，但仍无帧 —— 因为当时还缺 readyState 那一道修复）。
- **下一步就是：装 0.2.5.29 → 跑 `/values.html` → 读 `media` 行与 `media-state` 行。**

---

## 1. 死因链：三道门（这是本次交接最重要的东西）

不是三个 bug，是**三道独立的门**，任何一道没开都会表现为"同样的没画面"。全部结论都有真机读数或上游源码依据。

### 门 1：`<video>` 根本不存在（`pageConfigurationWithEmptyClients(std::nullopt, ...)`）

`port/WebCoreDriver.cpp` 四个建页点传了 null page identifier → `Page::mediaSessionManager()` 首行 `if (!m_identifier) return nullptr;` → `PlatformMediaSession::clientWillBeginPlayback()` 以 **false** 回调 → `completePlayInternal()` 永不执行 → `MediaPlayer::play()` 永不被调用。
**修法**：`PageIdentifier::generate()`。**已验证**：`paused` 从"永远 1"变成 0。

### 门 2：文档从未脱离挂起状态（`ActivityState::IsInWindow` 从未设置）

从 `play()` promise 被 `AbortError` 拒这一条往上追：

```
HTMLMediaElement::updateShouldPlay()                      HTMLMediaElement.cpp:9834
  → playbackStateChangePermitted(Playing) 被拒
  → pauseInternal()                                       HTMLMediaElement.cpp:4685
     → 置 paused=true + 以 AbortError 拒掉 pending 的 play promise   :4725
     → updatePlayState() → MediaPlayer::pause() → 后端 m_paused=true
        → 解码线程停在暂停分支 → reads=1 / 无帧 / readyState 卡在 HAVE_METADATA

拒绝理由：element->isSuspended()                          MediaElementSession.cpp:457
  = document().activeDOMObjectsAreSuspended()             HTMLMediaElement.cpp:9252
  ← ScriptExecutionContext::suspendActiveDOMObjects()    ScriptExecutionContext.cpp:424
  ← Document::suspendScheduledTasks()                    Document.cpp:8874
  ← LocalFrame::suspendActiveDOMObjectsAndAnimations()   LocalFrame.cpp:1145   ← 这是个计数器
  ← Page::setActivityState 的 IsInWindow 位              Page.cpp:3410
```

port 只调过 `page->setIsVisible(true)`（只动 IsVisible 位），**从未设 IsInWindow**。
**修法**：`wkPutPageInForeground()` → `page->setIsInWindow(true)`。

> ⚠️ **必须放在 `setView()` + `init()` 之后**。0.2.5.26 曾在 view 之前调 `setActivityState` 并连带 `WindowIsActive/IsFocused`，触发了焦点路径，**重演了 port 自己注释里记录的 0.1.0.9 崩因**（`selection().pageActivationChanged()` 不判空 → `null+0x858`，见 `WebCoreDriver.cpp:4064` 那段警告注释）。app 直接起不来，服务器日志里一条设备请求都没有。
> **0.2.5.27 的修法**：只加 `IsInWindow` 这一位（它才是门控位，且不走焦点路径），调用点移到 `buildSession` 里那两行焦点调用旁边、`WebCoreLoadUrl` 里移到 view resize 之后。**未验证**（装上后设备就睡了）。

### 门 3：元素与后端互相等待 `readyState`（本次新发现，最隐蔽）

```
HTMLMediaElement::updatePlayState()      HTMLMediaElement.cpp:6592
  shouldBePlaying = potentiallyPlaying()
  if (shouldBePlaying) { ... playPlayer(); }        ← 唯一到达 MediaPlayer::play() 的路径

HTMLMediaElement::potentiallyPlaying()   HTMLMediaElement.cpp:6454
  if (!couldPlayIfEnoughData()) return false;
  if (m_readyState >= HAVE_FUTURE_DATA) return true;              ← 需要 readyState ≥ 3
  return m_readyStateMaximum >= HAVE_FUTURE_DATA && ...;
```

`readyState` **只由后端上报**。而后端只在被要求播放时才解码，暂停时只预滚一帧就 park ⇒ 最多报到 `HaveCurrentData(2)` ⇒ `potentiallyPlaying()` 恒假 ⇒ `playPlayer()` 从不发生 ⇒ 后端 `m_paused` 恒真 ⇒ 解码线程恒 park ⇒ 更没有数据去抬高 `readyState`。**两边各等一步。**

真实后端都靠同一件事破解：**暂停时照样缓冲**并随进度上报 readyState。

**修法（0.2.5.29，三处，均在 `MediaPlayerPrivateWinUWP.cpp`）**：
1. `DecoderState::bufferedEnd` —— 记录已读到的媒体位置（在 `ReadSample` 成功后更新）；
2. 暂停分支：**先解码到 2 秒读前量（或 EOS）**，再在主线程（`callOnMainThread` + `ThreadSafeWeakPtr`）上报 `HaveFutureData`（EOS 时 `HaveEnoughData`），然后才 park；
3. 音频推送循环**不再因 `m_paused` 退出**（暂停也要填环，这是 readyState 无法推进的第二个原因）。

### 被证伪的假设（都别再试了）

| 假设 | 否证证据 |
|---|---|
| 自动播放策略（有声需手势） | `muted` 视频同样被拒；真实 `gesture-click` 也无效 |
| moov 在尾部导致回读失败 | `+faststart` 版（`fast.mp4`）行为完全一致 |
| 音频推送死锁在队列互斥量 | `cap=48000 qpush=0 cpush=0` —— pushPCM 从未被调用，try_lock 从未失败 |
| 网络等待 | MF 之后不再发请求，且无 ESTABLISHED 连接 |
| 视频管线坏了 | `native=0x34363248(H264) out=0x16(RGB32) dxva=on err=0`，协商成功 |

---

## 2. 顺手修掉的另一个真实缺陷：壳从不阻止息屏

`harness/` 从未持有 `DisplayRequest`。UWP 默认不阻止息屏，屏幕一灭系统就冻结/挂起前台进程 —— **视频随之停住**（用户可见），远程调试（WDP）也断连。这也是先前每轮真机测试都在半路断掉的原因。
**修法（0.2.5.28 起）**：`MainPage::HoldDisplayAwake()`，每次导航后持有 `DisplayRequest` 并重新计时，**10 分钟上限**（不无限耗电）。这同时让测试窗口变可靠：装好后屏幕会保持常亮 10 分钟。

---

## 3. 下一步（照这个顺序做）

```powershell
# 1. 确认设备在线（手机可能换了 IP，先看 ARP 表）
arp -a | Select-String '192\.168\.3\.'
# 若 IP 变了，下面所有 -Ip 换掉；Settings → 面向开发人员 → Device Portal 页面顶部有当前 URL

# 2. 安装 0.2.5.29（appx 已构建好）
pwsh -NoProfile -File E:\Apotheosis\tools\install-only.ps1 -Ip 192.168.3.159 -Ver 0.2.5.29

# 3. 跑测试（测试服务器必须先在 PC 上起着！）
pwsh -NoProfile -File E:\Apotheosis\tools\Test-MediaServer.ps1 -Port 8099   # 另开一个窗口
pwsh -NoProfile -File E:\Apotheosis\tools\Test-Media.ps1 -Path '/values.html' -WaitSeconds 40
```

**读什么**（`stage.txt` 的 `media` 行 + `media-state` 行，字段含义）：

```
media readers=1 frames=0/0 pcm=0/0/0 q=0@48000Hz/2ch native=0x34363248 out=0x16 dxva=on err=0x00000000 at=-
      reads=1 lasthr=0x0 flags=0x0 sidx=0 vidx=2 aidx=0 drop=0
      cap=48000 qpush=0 qcons=0 qdrop=0 und=0 cpush=0 phase=6
media-state vis=1 visocc=1 inwin=1 focused=1 winactive=1 idle=0 fsusp=0 dsusp=0
```

| 字段 | 含义 / 判断 |
|---|---|
| `frames=` | **第一判据**。`>0` 即解码在跑 |
| `ct`（页面上报） | **第二判据**。开始递增即时钟在走 |
| `media-state` 的 `fsusp` / `dsusp` | **门 2 的直接证据**。`1` = 帧/文档仍被挂起 ⇒ `IsInWindow` 没生效 |
| `reads=` | `ReadSample` 调用次数。`1` 且 `frames=0` ⇒ 线程停在暂停分支 |
| `phase=` | `handleAudioSample` 阶段：1 进入 / 2 sink / 3 ConvertToContiguousBuffer / 4 Lock / 5 格式 / 6 进 push 循环 / 7 push 返回 / 8 放弃本样本 |
| `qpush` / `cpush` | 队列自己的推送数 / 互斥量竞争数 |
| `err=` / `at=` | 引擎侧错误码与出错点 |

**判读**：
- `frames>0` 且 `ct` 递增 ⇒ **三道门全开**，转去补 DoD 剩余项（§5）。
- `dsusp=1` ⇒ 门 2 仍关：检查 `IsInWindow` 调用点是否真的在 view 之后、以及设备是否真的收到了这次安装（版本号对上）。
- `frames=0` 且 `reads` 很低、`dsusp=0` ⇒ 门 3 仍关：读 `phase` 与 `rs`（元素 readyState）—— 若 `rs` 仍是 1，说明 `HaveFutureData` 没上报成功。

---

## 4. 环境要点（都是踩过的坑）

- **ASCII 路径**：仓库 `E:\Apotheosis`，vcpkg `C:\vcpkg`。构建链对非 ASCII 敏感。
- **三套工具链**：引擎 WTF/JSC/WebCore = clang-cl（`--target=thumbv7-unknown-windows-msvc`）；`port/*.cpp` = clang-cl + lld-link；harness = MSVC v143 14.44.35207 ARM + SDK 22621 arm 库（`port\arm32-uwp-env.ps1` 手搓环境）。
- **C++ 异常必须关**：`_HAS_EXCEPTIONS=0` + `/EHs-c-`（clang 的 thumbv7-windows-msvc 无法 lower `cleanupret`）。
- **`WebKit/` 是独立 git repo 且 gitignored** —— `git add WebKit/...` 会被静默忽略。上游改动的唯一入库方式是重新生成补丁：
  ```powershell
  cd E:\Apotheosis\WebKit
  $new = @('Source/WebCore/platform/graphics/win/MediaPlayerPrivateWinUWP.h','...cpp','Source/WebCore/platform/graphics/win/WinUWPMediaBridge.h','Source/WebCore/PAL/pal/PlatformWinUWP.cmake')
  $all = $new + @('Source/WebCore/platform/graphics/MediaPlayer.cpp','Source/WebCore/PlatformWinUWP.cmake','Source/cmake/OptionsWinUWP.cmake')
  git add -N -- $new; git diff HEAD -- $all > E:\Apotheosis\patches\2026-10-09-winuwp-media-mf.patch; git reset -q -- $new
  ```
- **ninja 永远全量重建**（`stored deps info out of date`，`ninja -t restat` 无效，`CMakeFiles\WebCore.rsp` 不存在所以无法手改依赖）。全量 WebCore ≈ **1h45m**（-j 4）；只改 driver ≈ **25 min**（编译 ~4 min + 链接 ~6 min + 打包 ~12 min）。
- **install-only.ps1 在版本已安装时会跳过**；直接 `POST /api/app/packagemanager/package` 返回 202 却跑旧二进制 ⇒ **必须升版本号**。
- **设备睡眠**：息屏 → WiFi 断 → WDP 不可达，部署/测试全断。0.2.5.28 起壳会自己阻止息屏（导航后 10 分钟）。若 IP 变了，`arp -a | Select-String '192\.168\.3\.'` 看当前地址。
- **PowerShell 坑**：循环变量写 `${round}` 而非 `$round:`；`.get()` 在 `CheckedRef` 上返回引用（要 `&page.get()`）；前向声明若落进匿名命名空间会变成内部链接而编译失败。
- **测试服务器必须单独起着**：`tools\Test-MediaServer.ps1 -Port 8099`。它一停，页面就取不到（`stage.txt` 的 `firstbyte=-`），看起来像"引擎坏了"其实是"服务器没了"。清理重复进程时**别用会匹配到自身命令行的过滤条件**（我因此自杀过两个后台任务）。

---

## 5. 诊断埋点清单（`media` 行怎么扩出来的）

新增字段都在 `port/PortMediaAudio.cpp` 的 `appendMediaDiagnostics`；数据来自 `WinUWPMediaDiagnostics`（`WinUWPMediaBridge.h` + 播放器内的静态量）。**驱动侧编译用的是 `build-clang-gpu\WebCore\PrivateHeaders` 里复制过去的那份契约头**，所以 driver 依赖的新声明必须等 WebCore 编完才会刷新 —— 顺序永远是「先 WebKit 再 driver」。

---

## 6. 提交清单（`media-mf`）

```
c99c557  fix(media): 暂停时也要缓冲并上报 readyState 递进 —— 这才是"没有画面"的根因;另修壳不阻止息屏   ← 0.2.5.29，未验证
3036f7a  fix(media): 只加 IsInWindow,且必须在 setView()+init() 之后 —— 0.2.5.26 崩因更正
cc9a21e  fix(media): 页面从未被置于 IsInWindow 状态 → 文档挂起 → WebKit 拒绝播放
d775856  docs: 记录设备回合 3 —— 音频路径冻结视频路径(放大器式设计缺陷)
03a3f59  fix(media): 播放闸门(PageIdentifier) + 音频推送不再冻结整条管线;并加埋点
aba3ac9  patches: 媒体线的上游改动入库(含本次崩溃修复)
```

---

## 7. DoD 剩余项

| 项 | 状态 |
|---|---|
| ① 本地 mp4 有画面有声音 | ⏳ 待装 0.2.5.29 验证（三道门中门 3 的修复未验证） |
| ② 真实站点内联 mp4 | ⏳ 待①通 |
| ③ crash.txt 无新增 + stage.txt 媒体诊断 | ✅ 诊断行齐备。崩溃面：0.2.5.25 及更早零新增；**0.2.5.26 我引入过一次崩溃，已定位并更正**（见门 2 的警告） |
| ④ 抖音白屏单独验证 | ⏳ 待设备 |
| ⑤ 软/硬解功耗与发热记录 | ⏳ 待设备 |
| ⑥ 更新 `docs/FEATURE-GAPS-2026-10-08.md` §5 | 部分完成（已有实测结论），待①的最终数字补全 |

---

## 8. 方法论（这个项目值得留下的两条）

1. **无界等待会把局部问题放大成整条管线停摆。** 音频推送循环"故意"把满队列当背压并无限等待，于是一个音频侧问题冻结了整条媒体线（含视频）。队列头文件本来就写着"pushPCM() never waits for the audio callback"—— 队列遵守了契约，**调用方违背了它**。已在 0.2.5.25 改为有界（250ms 后放弃本样本并计 `dropped`）+ 队列 `try_lock`。
2. **UI 线程崩溃的栈符号化不出有用信息时，服务器侧"设备从未请求过页面"同样是硬证据。** 0.2.5.26 的 app 起不来，我没有去解栈（全是 `Windows.UI.Xaml`/`twinapi`/`minuser` 帧），而是看到测试服务器日志里**一条设备请求都没有** ⇒ app 没活到导航 ⇒ 往回查我改的那一行。这比栈更快、更可靠。

---

## 9. 详细记录

`docs/MEDIA-MF-IMPLEMENTATION.md` —— 按"设备回合 1..5"记录了每一轮的真机读数、被证伪的假设、以及上述三条根因链的完整推导。**下一轮测试结果请追加到那里。**

---

## 13. 920/930 (WDDM 1.x) 适配（2026-10-18，0.2.5.34 = `e74dee4`）

### 13.1 问题

ANGLE 后端只覆盖 **WDDM 2.0**（Lumia 950/950XL，真机验证多年）。**WDDM 1.x 机型
（920/930/925/928/1520/Icon/640/650…）上 GPU 路会炸**，且崩点不总在驱动的 SEH 壳内——
ANGLE 把 surface create/resize **marshal 回面板 dispatcher（UI 线程）**，那边什么都兜不住。
所以准入判定必须前移到任何 EGL 调用之前、按**设备身份**做，不能试探（试就炸）。

### 13.2 实现（harness-only，引擎未动）

- **判定**：`EasClientDeviceInformation.SystemProductName` 含 `"950"` → 放行；
  或物理分辨率 ≥1440 宽（WQHD 为 950 家族独占，名字缺失时的兜底）→ 放行；
  **其余一律软件路**（保守方向：未知机器不赌，Cairo 软渲染在所有 Win10M ARM 机上都能跑）。
- **门点**（`m_gpuAllowedByDevice` 一个旗）：
  | 入口 | 行为 |
  |---|---|
  | `EnableGpu()`（所有 GPU 启动的 choke point） | 拒绝并落 stage.txt |
  | `OnGpuPanelLoaded` 的自建三角形探针（不经 EnableGpu） | 单独查旗拒绝 |
  | autodiag 的离屏 `GpuInitGuarded` | 跳过，dump 里记 `gi=-1` |
  | 设置页 GPU 开关 | 强制关 + `IsEnabled=false`（防手动送命） |
  | `m_gpuDefault` | 落盘关掉，与开关一致 |
- **日志**：每次启动落 stage.txt 一行
  `gpu-gate: product='…' fw='…' mfr='…' phys=WxH -> gpu ALLOWED|FORCED-SOFTWARE`
  ——**下一轮设备回合即可拿到每台机器的真实 `SystemProductName`**，用来校准白名单
  （万一 950 上报的是 `RM-xxxx` 形式而不是 "Lumia 950"，白名单要补）。
- **DXVA 路不门控**：`D3D11CreateDevice` 自 WP8.1 起就在 920/930 可用（不是 ANGLE），
  且探测失败自动退软解，media 行会报实际状态（`sw-novideo`/`sw-mgrfail`）。

### 13.3 验证顺序（重要）

1. **先在 950 上跑 0.2.5.34**：stage.txt 应显示 `gpu-gate: product='Lumia 950…' phys=1440x2560
   -> gpu ALLOWED`，GPU 路照常（否则白名单判据要按真实身份串修）。
2. **再上 920/930**：应显示 `FORCED-SOFTWARE`，整机走软件路，能加载页面即算适配完成；
   `media` 行的 `dxva=` 会告诉我们老机器的 D3D11 视频能力（大概率 `sw-novideo`）。
> **设备矩阵（2026-10-18 用户确认，别假设所有 Lumia 一样）**：
> **Lumia 950（WDDM 2.0）** = ANGLE/D3D11 FL9_3 GPU 合成路可用（真机验证多年），也是 DXVA
> 实验的目标设备；**Lumia 930/920（WDDM 1.x）** = ANGLE 后端不适配，**硬件渲染会炸**，
> 那两款实际只能软件渲染（Cairo）。
> **DXVA 路本身在 930/920 上安全**：设备创建失败（无 D3D11 驱动）→ `sw-mgrfail`，或拿到
> WARP 设备 → QI `ID3D11VideoDevice` 返 `E_NOINTERFACE` → `sw-novideo`，两种都不设/
> 不用 manager，解码器自动退软解；且解码输出两条路都是 RGB32 系统内存（VP MFT 转换），
> 不碰 ANGLE/SwapChainPanel——**DXVA 不引入新的设备兼容维度**。会炸的只有 ANGLE 合成路
> （已在 `g_gpuActive` 门控，GPU 起不来回 Cairo）。
### 12.1 现状判定：**软解**，且 `dxva=on` 是假象

- `openSourceReader()` 只设 `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING` +
  `MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN`，**没有 `MF_SOURCE_READER_D3D_MANAGER`**；
- 全仓库无 `MFCreateDXGIDeviceManager`/`D3D11CreateDevice` 调用，解码线程没有 D3D 设备；
- SourceReader 的硬件 transform 以 D3D manager 为前提 ⇒ **H.264 一直跑在软件解码器上**；
- media 行 `dxva=on` 只是 `state.dxvaDisabled=false` 的回声（诊断字段设计为“没关 DXVA”，**不是**“DXVA 已生效”）。

### 12.2 硬件解码要补的（~40 行，解码线程内）

```cpp
MFCreateDXGIDeviceManager(&resetToken, &dxgiMgr);
D3D11CreateDevice(HARDWARE, D3D11_CREATE_DEVICE_VIDEO_SUPPORT|BGRA_SUPPORT,
                  {11_1,11_0,10_1,10_0,9_3}, &device);   // Win10M 第三方上限 FL9_3
dxgiMgr->ResetDevice(device);
attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, dxgiMgr);
```

VP MFT（已开）继续把 D3D 纹理转 RGB32 系统内存，**现有 paint 路零改动**。

### 12.3 核心未知数（必须真机实验）

Win10M 第三方 D3D11 上限 FL9_3。查证后的文档事实（2026-10-18，Microsoft Learn）：

- **UWP 只能走 D3D11 路**：`DXVA2CreateDirect3DDeviceManager9`（D3D9/DXVA2）文档
  Requirements 明确 "desktop apps only"，Remarks 原文 **"Windows Store apps must use
  IMFDXGIDeviceManager and Direct3D 11 Video APIs"**。D3D9 兜底路直接排除。
- **`ID3D11VideoDevice` 没有 FL 书面限制**：文档 Remarks 只说 WARP / 参考光栅化 /
  Microsoft Basic Display Adapter 会返回 `E_NOINTERFACE`，Requirements 是
  "Windows 8 [desktop apps | **UWP apps**]"。（此前"要 FL10_0+"是推测，**文档无此说法**。）
  Lumia 950 是真 WDDM 2.0 驱动（非 Basic Display），**有戏**；930/920 WDDM 1.x 的
  老驱动不暴露 video device，与实测吻合。
- MF 解码器在设备上找不到配置会**自动退软件解码**（`MF_E_UNSUPPORTED_D3D_TYPE` →
  Topology Loader 发 `MFT_MESSAGE_SET_D3D_MANAGER=NULL` 重协商）⇒ 实验零风险。

**判据（0.2.5.33 已实现，等真机）**：设备上 QI `ID3D11VideoDevice` +
`GetVideoDecoderProfileCount()`，并枚举是否有 H.264 VLD profile——有 = 驱动支持硬解；
配合 `MF_SOURCE_READER_D3D_MANAGER` 设上，解码器即走 DXVA。结果打进 media 行新增的
`dxvaprof=` / `dxvafl=` / `h264=` 三字段，`dxva=` 改报 `hw` / `sw-nomgr` /
`sw-novideo` / `sw-mgrfail` 实际状态（原来是 `on/off` 回声，见 §12.1）。

### 12.4 收益与优先级

- 当前 320x176@30fps 软解在 A53 上约 10-15% CPU，小片无所谓；
- 硬解真实收益：**功耗/发热（DoD ⑤）+ 480p/720p 能力**（抖音/B 站门槛）；
- 优先级：音频之后（用户已确认手机能正常出声 ⇒ AudioGraph null-sink 是 port 问题）。

### 12.5 下一版（0.2.5.33）打包清单

1. **音频端点探测**：枚举 `DeviceInformation.FindAllAsync(DeviceClass.AudioRender)` 打进 mediaaudio.txt；
2. **DXVA 探针**：按 §12.2/§12.3 实现 + 探针 QI 判定，结果打进 media 行（如 `dxva=probe-ok/probe-fail/off`）；
3. **音频图空转自停**：连续 N 个 quantum 取 0 帧即 `Stop()`（避开 null-sink 空转 ~5 分钟后在 Windows.Media.Audio.dll 内 faults 的问题，见 §11.7）；
4. 声音本体方案二选一（依端点探测结果）：MF 音频渲染器 `MFCreateAudioRendererActivate`，或修 AudioGraph 端点。

**状态：0.2.5.33 已实现并提交（`28d313f`），构建中。** 1/2/3 全部落地；4 待端点探测结果再定
（若枚举不到设备 = App Container 拿不到端点，走 MF 音频渲染器；能枚举到 = 查 AudioGraph
设置/激活问题）。

---

## 11. 0.2.5.30–0.2.5.32 回合（2026-10-18 傍晚，用户在场手动导航）

### 11.1 第一批 media 行（19:09 轮，地址栏手动导航 values.html）

用户手动导航（testurl 自动导航钩子当天失效，原因见 §11.5），拿到**媒体线历史上第一份真实读数**：

```
media readers=1 frames=25/1 pcm=48000/0/39296 q=48000@48000Hz/2ch native=0x34363248 out=0x16 dxva=on err=0
     reads=75 vidx=2 aidx=0 drop=0 cap=48000 qpush=48000 qcons=0 qdrop=39296 und=0 cpush=0 phase=7
```

- 文件是真的：H264→RGB32 协商成功、DXVA 开、零错误、元数据 320x176/10.03s。
- 元素侧：`canplay rs=3` → `playing` → `ct` 实时推进到 9.8s（时钟是墙钟，**推进不解码**）。
- 用户观感：**进度条走、无画面**。

### 11.2 音频图是好的（19:18 轮 + mediaaudio.txt 诊断）

0.2.5.31 给壳加了音频诊断（`MediaAudio` 各决策点落盘 `LocalState\mediaaudio.txt`）。19:18 轮：

```
19:18:36 EnsureStarted: early-return (no format yet)
19:18:38 EnsureStarted: starting graph for 48000Hz/2ch
19:18:39 StartGraphFor: graph created
19:18:39 StartGraphFor: graph STARTED (quantum handler hooked)
19:18:39 quantum #1 required=480 taken=480 underran=0     ← 第一帧就被取走
...
19:23:15 quantum #465300 required=480 taken=0 underran=1  ← 播完后空转
```

- **AudioGraph 1 秒内就起来并抽干环**（`qcons=477056`）。§11.1 的 `qcons=0` 是 media 行的**快照时机**造成的误读（导航早期写的），图起来后解码线程立刻满速恢复：`frames=299`（整部解完）、视频播到 `ended=1`。
- 存疑点：空转时 quantum 频率 ~1700/s（真实设备应 ~100/s），怀疑没有真实输出端点（null sink 空转）——**声音是否可听尚未验证**，是下一步。

### 11.3 “没画面”的最终根因：`supportsAcceleratedRendering()=true`

`RenderVideo::paintReplaced()`（RenderVideo.cpp:298-302）：

```cpp
// Avoid unnecessary paints by skipping software painting if the renderer is accelerated...
if (hasAcceleratedCompositing() && videoElement->supportsAcceleratedRendering()
    && !paintBehavior(FlattenCompositingLayers) && !paintBehavior(Snapshotting))
    return;                      // ← 跳过 CPU paint
videoElement->paint(context, paintRect);
```

GPU 合成中 + 后端声称加速渲染 ⇒ WebKit 直接跳过软件 paint，把画面交给**合成器的媒体平台层**。本后端没有平台层（`platformLayer()` 默认 nullptr），设计就是 CPU BGRA + `drawNativeImage`（MediaPlayerPrivateWinUWP.h 头注释原话："no new present path is introduced"）。`true` 让 WebKit 去等一条不存在的路——真机读数 **`frames=299/2`**：299 帧全部解出、只有 2 帧画上去（合成启用前的软件帧），视频区全黑。进度条能动是因为它是另一个元素，走正常 repaint。

**修法（0.2.5.32，`101ed9e`）**：`supportsAcceleratedRendering()` 改 `false`，每帧都走 `paint()` → `drawNativeImage` → layer backing store，Cairo/TextureMapper 两条呈现路都能显示。**待装 0.2.5.32 验证。**

### 11.4 0.2.5.30 的 readyState 修复已验证生效

19:18/19:19 两轮全程 `rs=3`（`t6 rs=3 ct=3.732`、`t12 rs=3 ct=9.802`），不再出现 0.2.5.29 的 `waiting rs=2`/`stalled rs=2` 逐帧振荡（`stalled` 事件仍会出现但 `rs` 保持 3，无害）。

### 11.5 当天踩过的部署/工具坑（续 §10.3）

- **testurl.txt 自动导航钩子当天全程失效**（内容正确、37 字节干净，但 app 启动不读它；10/18 凌晨和今早的 mem.txt 里大量 `startup gpu-first url=...` 证明代码是好的）。手动开 app 大概率是“恢复旧进程”而非新启动（构造函数不跑 ⇒ 不读 testurl）。**测试改用地址栏手动导航，不受影响。**
- **WDP 推文件（写 LocalState）500 "Access is denied"** 依旧（`path=%5CLocalState` 形态）；GET 正常。改文件内容这条路不通，靠设备上已有文件 + 手动导航。
- 媒体诊断计数器是**进程级累计**（`readers=`/`frames=`/`pcm=` 不随导航清零），stage.txt 每篇导航落一行——对比两行才能看出恢复过程。
- `mediaaudio.txt` / `silent.html`（纯视频隔离页，`silent.mp4` = test.mp4 去音轨）为新增诊断面。

### 11.6 下一步

1. ~~**装 0.2.5.32 → 地址栏导 `values.html`**：判据 `frames=N/M` 且 **M 随 N 增长**（画面在动）、`pcm` consumed>0（环在抽）。`M` 仍卡在个位数 = paint 路径还有问题。~~
   ✅ **0.2.5.32 已装（20:35）、用户真机确认"能动了"**（20:36 轮，silent.html 全程 `rs=3`，播到 `ended=1`）。paint 路径修复生效，**"有画面"闭环**。累计计数（`frames=N/M`）待用户再刷一次留档。
2. **声音换方案**：见 §11.7。
3. 抖音白屏（DoD ④）、软/硬解功耗（DoD ⑤）仍待设备。

### 11.7 声音：AudioGraph 在此设备上不可用（0.2.5.32 轮新证据）

20:36 轮（纯视频）+ 19:18 轮（带音频）对比出的结论：

- AudioGraph `CreateAsync` 返回 Success、quantum  handler 正常挂上、**环也被抽干**（`qcons=477056`）——链路"通"。
- 但 quantum 频率 **~1700/s**（480 样本/quantum ⇒ 17 倍实时），真实设备应 ~100/s ⇒ **没有真实输出端点，graph 在 null sink 上全速空转**。
- 空转 ~5 分钟后 **在 `Windows.Media.Audio.dll` 内部 faults**：crash.txt 新增 4 条 `2026-10-18 19:24:09 tid=10180`（`reason: jit pool ... isJITPC(fault)=0`，栈帧 4-13 全在 Windows.Media.Audio.dll，由 Harness.exe 调用进入）。我们的 handler 每秒上千次 LockBuffer/CreateReference/GetBuffer/AddFrame 的对象churn 是诱因之一，但根因是端点缺失。
- 结论：**Win10M 15254 的 App Container 里这条 AudioGraph 路走不通**（要么端点打不开，要么 UWP 音频栈缺陷）。下一步候选：
  a. **MF 音频渲染器**：`MFCreateAudioRendererActivate` + `MFCreateAudioRenderer`（MF 在 App Container 已证明可用——SourceReader 就在用），把解码后的 PCM 直接交给 MF 渲染器，绕开 AudioGraph；
  b. 先写个最小复现（App 启动即建 graph、不碰媒体），确认是"端点打不开"还是"我们喂法不对"；
  c. 若确认端点不可用：声音降级为"仅解码计数 + 无输出"，把 DoD ① 改成"有画面、音频待硬件路径"。

---

## 10. 0.2.5.30 回合（2026-10-18 夜间，设备睡眠期间的自主工作）

### 10.1 真机读数：10/18 01:44 轮（0.2.5.29）—— 三道门全开的第一份证据

服务器日志 `port/_mediatest/server.log` 里 01:44 那轮（用户在 01:42 更新本文档后两分钟跑的）：

```
01:44:04.6  evt-loadedmetadata rs=1 ct=0.000 dur=10.03 wh=320x176 paused=0
01:44:04.8  evt-loadeddata    rs=2 ct=0.000
01:44:06.3  evt-canplay       rs=3 ct=0.001   ← 门 3 修复生效：HaveFutureData 上报成功
01:44:06.3  evt-playing       rs=3 ct=0.003   ← play() 被调用，时钟启动
01:44:08.4  evt-waiting       rs=2 ct=2.101   ← 预缓冲的 2s 消费完，新帧开始到达
01:44:09.3  evt-stalled       rs=2 ct=3.035
01:44:10.3  t6                rs=2 ct=3.967   ← 之后页面停止自报（息屏挂起应用）
```

**仍未拿到**：`stage.txt` 的 `media` 行（`frames=`/`pcm=`）——0.2.5.29 从未在 media 行可读的时刻被拉取过。注意 `ct` 递增**不等于有画面**：本后端的时钟是 `play()` 锚定的墙钟（`clockNowLocked()`），与解码无关。**画面/声音仍无直接证据，DoD ① 未闭环。**

### 10.2 新根因：播放中 readyState 被每一帧打回 HAVE_CURRENT_DATA

`MediaPlayerPrivateWinUWP::notifyFrameReady()` 对**每一个解码帧**无条件 `setReadyState(HaveCurrentData)`。播放中新帧持续到达 → rs 3→2 → 元素按 spec（HTMLMediaElement.cpp:3332，4.8.10.8）逐帧触发 `waiting`/`stalled`。01:44 日志的时间点（ct≈2.0 整，预缓冲耗尽点）与根因严丝合缝。

时钟没停是因为 `HTMLMediaElement::potentiallyPlaying()` 有 `m_readyStateMaximum` 回退（HTMLMediaElement.cpp:6465）——但页面和任何 JS 播放器逻辑看到的是"永久在缓冲"的元素（转圈、自定义重试都会被打断）。

**修法（0.2.5.30）**：播放中（`!m_paused && !m_reachedEnd`）一帧到达 = 数据在流动 → 上报 `HaveFutureData`；暂停时维持 `HaveCurrentData`（预缓冲路径自行抬高，见 §1 门 3）。且**只升不降**——合法下降（seek/新 load）各有其通知路径（`notifySeekFinished`/`load`）。补丁已重生成入库（`patches/2026-10-09-winuwp-media-mf.patch`，132,377 B，反向自检通过），单文件编译通过，全量重建见下。

### 10.3 WDP 部署坑（今天全踩了一遍；远程自动化必再撞上）

| 坑 | 现象 / 修法 |
|---|---|
| **本机 .NET `HttpClient` TLS 握手间歇失败** | 同样代码有时通有时 90s 全超时；`Invoke-WebRequest -NoProxy` 和 `curl.exe` 每次都通。**设备交互统一用 curl**（见 `tools/_media-round-curl.ps1`） |
| **WDP 文件上传（推 testurl.txt）500 "Access is denied"（0x80070005）** | `POST /api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=…&path=%5CLocalState`，multipart 格式与 auto-diag.ps1 完全一致仍 500；GET 同路径正常。终止 app 再推也没用。**当前设备上 testurl.txt 内容恰是对的，重启 app 即生效**；推送链路待查 |
| **curl 启动 app 要 `Content-Length: 0`** | 否则 411 Length Required（install-only.ps1 的 HttpClient 不发 body 反而没事） |
| **CSRF**：每个响应都会轮换 token | 必须「本次响应里的 Set-Cookie → 下一次请求的 `X-CSRF-Token`」；GET 不校验，POST/DELETE 校验 |
| PowerShell 命名陷阱 | `Iwr` 是 `Invoke-WebRequest` 的内置别名，**不能用作函数名**（别名优先于函数）；CookieContainer 在 `System.Net` 不在 `System.Net.Http` |
| `install-only.ps1` 版本已装时**跳过安装** | 直接启动。设备上 0.2.5.29 早已装好（10/18 凌晨用户装的），今天的"部署"实际是重启 |

### 10.4 当前状态与下一步

- **0.2.5.30 已构建完成**：`harness/AppPackages/Harness/Harness_0.2.5.30_ARM_Test/Harness_0.2.5.30_ARM.appx`（54 MB，Identity 0.2.5.30，Harness.exe 哈希与 0.2.5.29 不同 = 新引擎已入库）。唯一改动 = §10.2 的 readyState 修复；补丁已重生成入库并提交（`ba5ea6f`）。构建踩坑两则：**16GB 机 -j 4 会 OOM**（`LLVM ERROR: out of memory`，改 -j 2）；**MSBuild 起 CL.exe 时环境里 `NO_PROXY`/`no_proxy` 大小写重复会炸**（`MSB6001 ... 已添加项`，后台作业会被注入大写版，构建前把 *_PROXY 全清）。
- **设备 10/18 09:50 左右彻底睡眠**（ping 不通、443 关闭），符合§4"息屏→WDP 死"。
- **已挂全自动任务**：`tools/_auto-round.ps1`（等设备上线≤3h → `install-only.ps1 -Ver 0.2.5.30` → curl 版测试轮 → 证据写 `port/_mediatest/auto-round-result.txt`）。**注意：轮次要有效，手机必须亮着屏**——DisplayRequest 只防息屏不能点亮屏幕，息屏启动的 app 会被挂起（01:44 轮就死在 t6→t12 之间）。
- **设备一醒（人工）按序做**：
  1. 手机亮屏、解锁
  2. 若自动任务还没跑：`pwsh -File E:\Apotheosis\tools\_deploy-and-round.ps1 -Ver 0.2.5.30`
  3. 读 `media` 行：**`frames>0` = 有画面，`pcm` 的 pushed/consumed>0 = 有声音**；`qpush>0` 而 `cpush=0` = 音频没进设备。同时确认不再出现 `evt-waiting`/`evt-stalled`（§10.2 修复的判据）
  4. 测试服务器必须活着：`tools\Test-MediaServer.ps1 -Port 8099`（PID 11768 从 10/13 起常驻，根目录 `port\_mediatest`）
