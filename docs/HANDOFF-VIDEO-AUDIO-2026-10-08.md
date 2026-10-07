# 视频 / 音频 / 硬解 交接单 — 2026-10-08

> 给接手这条线的 agent。**单开一个 worktree/branch**（建议 `media-mf`），别在 `gpu-path1` 上做。
> 本单只描述现状、方案与坑，不含实现。

## 0. 一句话现状

`<video>` / `<audio>` **完全不能播**：`ENABLE_VIDEO=0`、`ENABLE_WEB_AUDIO=0`、`USE_MEDIA_FOUNDATION=0`，
`MediaPlayerPrivateMediaFoundation*.cpp` 与 `modern-media-controls` 资源被 DROP。
表现：标签渲染成空控件，无画面无声，`.play()` **不报错**（这就是最坑的地方——页面 JS 以为播上了）。

⚠️ 连带一个已知 UX 灾难：抖音电脑版白屏，根因就是它的 React 包初始化时调 `video.play()`
直接抛异常、整个 app 初始化中断（`docs/HANDOFF.md` §6 记着）。**修好视频这一条会顺带修掉它。**

## 1. 先读这些

1. `AGENTS.md` —— 铁律（ASCII 路径、三套工具链分工、`_HAS_EXCEPTIONS=0` + `/EHs-c-`、
   所有上游改动 `#if defined(WK_WINUWP)` + `Apotheosis:` 注释、单引擎线程铁律）。
2. `docs/FEATURE-GAPS-2026-10-08.md` §1 主表 + §5 —— 缺口清单和本单的方案来源。
3. `WebKit/Source/WebCore/PlatformWinUWP.cmake` —— **权威的"哪些平台源码没编"**，EXPLICITLY
   DROPPED 段逐条带原因。要恢复的视频源都在这儿。
4. `build-clang-gpu/cmakeconfig.h` —— 构建开关的**产物真相**（别信 cmake 文本，编译进去的才算）。
5. `docs/JIT-254-PORTING.md` / `docs/JIT-HANDOFF.md` —— JIT 线的撞墙顺序与 "-j4" 教训（**构建
   并行度必须 `ninja -j4`**，12 核 16G 机器开默认并行度会 OOM，本会话实测过一次）。

## 2. 设备与环境

- 设备 Lumia 950, Win10M 15254 (armfre.feature2_rs3svc), ARM32, App Container。
- 构建目录 `build-clang-gpu`（GPU+JIT 线，**要动的就是这条**）；另有 build-clang-jit /
  build-clang-webcore，三份 cmakeconfig 的 WebCore ENABLE_* 逐个相同，只 ENABLE_JIT 不同。
- 引擎链：clang-cl `--target=thumbv7-windows-msvc`。改引擎源后：
  ```powershell
  . E:\Apotheosis\port\arm32-uwp-env.ps1
  & "C:\Program Files\Microsoft Visual Studio\18\Community\...\Ninja\ninja.exe" -j 4 -C E:\Apotheosis\build-clang-gpu WebCore
  pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1
  pwsh -File E:\Apotheosis\port\build-harness.ps1
  ```
  注意：**全量重建是常态**（改任何 WTF 头 → 上千 TU），`ninja -j4` 一轮 1006 步约 60-80 分钟。
- 部署：`tools\install-only.ps1 -Ip 192.168.3.159 -Ver <ver>`（**非破坏**，不卸载数据）。
  `deploy-launch.ps1` 会先卸载 → 清掉 LocalState，调试期别用。
- `Deploy-Robust.ps1` 有已知**假成功**：上传 202 后首次安装 state 可能还是 204，它就当结束、
  查到旧版、launch 500 也打印成功。必须自己再 `GET /api/app/packagemanager/packages` 确认真实版本。
- x64 构建机跑不了 ARM32 appx，**每个引擎改动都要真机回合**。

## 3. 要动的开关与源

`OptionsWinUWP.cmake` / `PlatformWinUWP.cmake` 里：

| 开关 | 现值 | 改成 |
|---|---|---|
| `ENABLE_VIDEO` | 0 | 1 |
| `ENABLE_WEB_AUDIO` | 0 | 1（要 AudioContext / 音效） |
| `USE_MEDIA_FOUNDATION` | 0 | 1 |
| `ENABLE_MEDIA_SESSION` | 0 | 建议 1（锁屏/通知栏媒体控制，Win10M 有 GlobalSystemMediaTransportControls） |
| `ENABLE_MSE` (MEDIA_SOURCE) | 0 | 第二轮；第一轮先播完整 mp4 |

要取消 DROP 的源（按依赖顺序）：
```
modern-media-controls 资源(MediaControlsHost 等)
platform/win/MediaPlayerPrivateMediaFoundation.cpp
platform/win/MediaPlayerPrivateMediaFoundationCairo.cpp
```
⚠️ 上游这套是为 **Win32 桌面**写的（`HWND` / `DComp` / `DirectComposition` / `DXVA2`），
App Container 下**大概率不能直接编过**。别指望原样恢复，按 §4 重写的可能性更大。

## 4. 建议方案（按投入产出排序）

### A. 最小闭环（先做这个）
一个 `<video>` 测试页放**本地 mp4**（塞进 appx 或 testurl.txt 用 data:/file: URL），
目标只是"能出画面、能出声"。

实现选 **Media Foundation 的 `MFCreateSourceReaderFromURL` + `IMFMediaBuffer` → 手动送
cairo/TextureMapper surface**，而不是上游的 EVR/DComp 呈现链。理由：
- App Container 里 EVR/DComp 的窗口呈现基本不可用（没有 HWND）；
- SourceReader 是 MF 里最少依赖窗口的入口，硬解（DXVA2/H.264）也走它；
- 我们已经有两条成熟的呈现路：Cairo `paintToRGBA` 和 TextureMapper→ANGLE swapchain，
  视频帧当作一张动态纹理喂进去即可，与合成链路一致。
- 音频输出用 **WASAPI（IAudioClient）**；App Container 下 WASAPI render 可用（capability
  `internetClient` 不够，但音频输出不需要额外 capability）。

### B. 硬解
Win10M 15254 的 MF 带 DXVA2 transform。路径：SourceReader 上 `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING`
+ `MF_READER_DISABLE_DXVA` **不设**（默认允许 DXVA2），或手工 insert `CLSID_CResizerDo` 之类。
**App Container 下 DXVA2 可用性必须实测** —— 如果不通，退回软件解码（MF 自带），
至少能出画面，功耗差一些。**先证软件、再求硬解**，别一上来赌 DXVA2。

### C. 控制条与全屏
`modern-media-controls` 被 DROP 了；第一轮先不做 WebCore 那套 Shadow DOM 控件，
用 harness 原生 XAML 覆盖层（本仓刚给长按菜单加过同一套 overlay 模式，`MainPage.xaml`
的 LinkMenuCard / MessageDialog 可照抄）。全屏 `ENABLE_FULLSCREEN_API` 另说。

### D. Web Audio / MSE / WebRTC
排在 A/B 之后。MSE（`MediaSource`）要另一套 `MediaSourcePrivateClient` 接线；
WebRTC 在 Win10M 上基本不现实（`ENABLE_WEB_RTC=0`、`MediaStream`/`getUserMedia` 全无），
**建议明确不做**，别摊薄精力。

## 5. 已知坑（别重新踩）

- **`_HAS_EXCEPTIONS=0`**：整棵树不能抛 C++ 异常。MF 全是 HRESULT 返回，倒是天然适配；
  但 PPL/`concurrency::create_task` 那些 C++/CX 侧是 harness 的（MSVC），那边可以有异常。
- **单引擎线程铁律**：present 只在引擎线程；UI 线程绝不等引擎。视频解码线程要往合成线程
  投帧时走引擎已有的 queue/post 模式，别新造一个同步点。
- **`g_gpuActive` 门**：`port/WebCoreDriver.cpp` 里 `PortChromeClient` 只在 GPU 成功后装上
  （`if (g_gpuActive)`）。GPU 起不来 → 页面跑在 EmptyChromeClient 上，视频路径要能在软件
  回退下也活着。这是 `docs/FEATURE-GAPS-2026-10-08.md` §0① 记的结构性问题。
- **App Container 能力**：appxmanifest 已有 `codeGeneration` / `internetClient`；
  本地文件播放要 `musicLibrary`/`videosLibrary` 或走 appx 内资源。
- **cacert.pem**：HTTPS 在 App Container 无系统证书库，已由 `WebCoreSetCACertPath` 注入，
  视频 URLs 不需要额外处理。
- **设备 Wi-Fi 极不稳定**，部署断一半是常态，用重试。

## 6. 验收标准（DoD）

1. `<video src=本地.mp4 controls>` 在设备上**出画面 + 出声**，进度条/播放/暂停可用
   （自绘 overlay 也算）。
2. 至少一个真实站点带内嵌 mp4 的页面能播。
3. `crash.txt` 无新增记录；`stage.txt` 里视频路径有基本诊断（帧数/解码方式）。
4. 抖音电脑版白屏是否顺带解决 —— **要单独验**，别假定。
5. 功耗/发热记录一版（软解 vs 硬解），给用户选默认。
6. `docs/FEATURE-GAPS-2026-10-08.md` §5 更新为实测结论。

## 7. 工作树与提交纪律

- 分支从 `gpu-path1` 切 `media-mf`；`gpu-path1` 近期有两条提交别丢：
  ```
  089323c fix: 0.2.4.4->0.2.5.x 交互闪退三部曲（jitStubs / RunLoop / select popup）
  549ae6f feat: 引擎侧复制粘贴 + 长按上下文菜单(harness 原生卡)
  ```
- 每完成一个可编译单元就提交；改上游 WebKit 源一律 `#if defined(WK_WINUWP)` + `Apotheosis:`。
- `wk-winuwp.patch` 本来就没在同步（`WK_WINUWP-PATCH.md` 也未同步），这条线别管它，
  但在提交信息里写清动了哪些上游文件。
- 工作树里有历史遗留的未跟踪临时脚本（`_*.ps1`）和未提交的 harness/driver 基线改动，
  **不要整库回退、不要全部提交**，只 `git add` 你自己动的文件。

## 8. 现在设备上的状态

- 装机版本 **0.2.5.15**（含本会话全部修复）。
- `LocalState/jitdiag.txt = 1`（JIT thunks 全开、baseline 关，1.0.0.0 前的默认调试态）。
- `LocalState/testurl.txt` 已删除，用户首页设置未被改写。
