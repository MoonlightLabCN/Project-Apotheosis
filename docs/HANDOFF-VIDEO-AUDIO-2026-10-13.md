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
