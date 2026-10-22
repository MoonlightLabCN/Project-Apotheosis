# 任务书：硬件解码 + MSE（Bilibili）—— 请先读完再动手

**日期**：2026-10-21
**分支**：`media-mf`（开发基线 `0.8.0.0`，出货版 `0.8.0.4`）
**优先级**：支线 A（硬解）必须先出结论，支线 B（MSE）依赖 A 的架构选型
**设备**：Lumia 950（ARM32 / UWP App Container / Win10M 15254.603 / WDDM 2.0）

---

## 0. 一句话

**硬解很可能没死——我们漏了 `ID3D11Multithread::SetMultithreadProtected(TRUE)`。**
而硬解和 MSE（Bilibili）很可能是**同一个解**：`Windows.Media.Playback.MediaPlayer`（系统组件，
内部自带 DXVA 硬解 + 原生 DASH/HLS），这条路我们**从未试过**。

---

## 1. 当前实现与已知事实（勿重复踩坑）

### 1.1 现有媒体后端（软解，已真机验证有声有画，**别弄坏**）

| 文件 | 作用 |
|---|---|
| `WebKit/Source/WebCore/platform/graphics/win/MediaPlayerPrivateWinUWP.{h,cpp}` | 主后端：MF `IMFSourceReader` + Video Processor MFT → RGB32 → `GraphicsContext::drawNativeImage` |
| `WebKit/Source/WebCore/platform/graphics/win/WinUWPMediaBridge.h` | 诊断桥（dxva 能力/探针/流索引） |
| `port/PortMediaAudio.cpp` | 音频环（bounded PCM ring） |
| `harness/MediaAudio.cpp` | UWP `AudioGraph` + `AudioFrameInputNode` + **`AudioDeviceOutputNode`** |

⚠️ **音频刚修好（0.8.0.2 真机确认"听到声音了"）**，根因是**图里漏建 `AudioDeviceOutputNode` /
没 `AddOutgoingConnection`**（悬空输入节点 → 17 倍速空转丢帧）。改动音频相关代码前务必读
`docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md` §15。

### 1.2 关键拒绝点（MSE 在此被挡）

```cpp
// MediaPlayerPrivateWinUWP.cpp:1581
MediaPlayer::SupportsType MediaPlayerPrivateWinUWP::supportsType(const MediaEngineSupportParameters& parameters)
{
    // MSE and MediaStream have no backend here; saying "maybe" would make the element wait
    // for source buffers or tracks that can never arrive.
    if (parameters.platformType != PlatformMediaDecodingType::FileOrHLS)
        return MediaPlayer::SupportsType::IsNotSupported;   // ← Bilibili 死在这里
    ...
}
```

### 1.3 构建开关现状（`build-clang-gpu/CMakeCache.txt` 实测）

| 开关 | 我们 | MetroSurf(`WP81` 分支) |
|---|---|---|
| `ENABLE_MEDIA_SOURCE` | **OFF** | **ON** |
| `ENABLE_VIDEO` | ON | ON |
| `ENABLE_TOUCH_EVENTS` | OFF | ON |
| `ENABLE_ASYNC_SCROLLING` | OFF | ON |
| `USE_MEDIA_FOUNDATION` | **ON** | **OFF** |
| `USE_COORDINATED_GRAPHICS` | OFF（用 TextureMapper） | ON |

### 1.4 历史结论（**有一条要推翻**）

- ✅ **真**：设备有能力。`reportDxvaCapabilityOnce()` 真机读数 = **FL 11_0** + **10 个解码 profile** + H.264 支持。
- ✅ **真**：主 reader 设 `MF_SOURCE_READER_D3D_MANAGER` + `ENABLE_VIDEO_PROCESSING` → `MFCreateSourceReaderFromURL` 直接 `E_FAIL`，**整条视频丢失**（0.2.5.34，`readers=0`）。15254 上**没有**文档承诺的 `MF_E_UNSUPPORTED_D3D_TYPE` 优雅退路。
- ⚠️ **要推翻/重审**：§12.7 的"DXVA 此路不通"结论 —— **因为从未调用 `ID3D11Multithread::SetMultithreadProtected(TRUE)`**（`git log -S` 零命中）。这是 MF 硬解的标准前置，缺失时 D3D 设备跨线程共享会失败，**表现正是我们看到的 E_FAIL**。

---

## 2. 支线 A：硬件解码（先做）

### A1. 第一步（成本最低，可能直接解决）

在 `reportDxvaCapabilityOnce()` 创建设备后、以及任何要用 DXGI device manager 的路径上补：

```cpp
COMPtr<ID3D11Multithread> mt;
if (SUCCEEDED(d3dDevice->QueryInterface(IID_PPV_ARGS(&mt))))
    mt->SetMultithreadProtected(TRUE);
```

然后**用一次性探针**（不是主 reader！）验证带 `MF_SOURCE_READER_D3D_MANAGER` 的 reader 能否创建。
探针范式参考已删除的 `probeDxvaWithReader()`（见 `git show 0f41eed` 的父提交）。

### A2. 若 A1 仍失败 → `MediaPlayer` 帧服务路线

SDK 已确认存在（`windows.media.playback.h` 实测）：

| API | 用途 |
|---|---|
| `MediaPlayer.IsVideoFrameServerEnabled` | ✅ 存在 —— 打开帧服务 |
| `MediaPlayer.VideoFrameAvailable` | ✅ 存在 —— 取解码帧事件 |
| `MediaPlayer.IsProtected` | ✅ 存在 |

思路：让系统 `MediaPlayer` 做硬解，从 `VideoFrameAvailable` 拿帧（可能直接是 GPU 纹理 → 零拷贝进
TextureMapper；也可能是 `IDirect3DSurface`，需 `ISoftwareBitmap`/`IMFMediaBuffer` 转换），
再交给现有呈现链。**这条路完全绕开自己管 D3D 设备/DXGI manager 的生命周期。**

### A3. 判据（必须真机）

- `media` 行的 `dxva=` 从 `sw` 变为硬件标记
- 1080p 播放时 CPU 占用显著下降（软解 320×176 已经很轻松，**必须用 720p/1080p 素材测**才有区分度）
- 不回归：现有软解路 + 音频必须照常

---

## 3. 支线 B：MSE / Bilibili（依赖 A 的选型）

### B1. 侦察优先（不改代码，先出结论）

SDK 已确认 **`Windows.Media.Streaming.Adaptive`（`AdaptiveMediaSource`）存在** ——
即 **DASH/HLS 是 OS 原生支持的**，而 Bilibili 分片流本质就是 DASH。

要回答的问题：
1. `AdaptiveMediaSource.CreateFromUriAsync` 在 **App Container + 15254** 上能否创建成功？
2. 能否吃 B站真实的 DASH 清单（需要 UA/Referer，B站有防盗链——**注意我们引擎的 curl 层可以带 header**）？
3. `MediaPlayer` + `AdaptiveMediaSource` 播放时，`VideoFrameAvailable` 是否仍给帧？

### B2. 若 B1 可行 → 接进 `MediaPlayerPrivateWinUWP`

作为新的 `platformType` 分支，`supportsType()` 放开 `MSE`/`MediaSource` 到可支持。

### B3. 若 B1 不可行 → 借鉴 MetroSurf

它的 `WP81` 分支打开了 `ENABLE_MEDIA_SOURCE=ON`，且 MSE 是**上游完整实现**
（`MediaSource.cpp` 1564 行 + `SourceBufferPrivate.cpp` 1977 行，仅 1 处 `#if defined(WP81)`）。

> ⚠️ **不要整套照搬 `WP81`**：它关掉了 MF、开了协调图形、改静态库、用 `_WINDOWS` 而非 App Container
> 那套，是**互斥的另一条架构路线**。只借鉴 MSE 接缝，别动我们的 MF 软解底座。

---

## 4. 已 fork 的参考仓库（可读）

- **Fork**：`https://github.com/Jimmyxiao2009/MetroSurf`
- **上游**：`MetroSurf/MetroSurf`（基于 **webkitgtk-2.54.1**，比我们 2.52.4 新）
- **本地**：`E:\Apotheosis\_metrosurf`（已 gitignore）
- **评估报告**：`docs/METROSURF-ASSESSMENT.md`（含它领先我们的模块清单：WebSockets / Storage /
  CacheStorage / IndexedDB / MSE / ARMv7 JIT 线 / mimalloc）

---

## 5. 工作纪律（违反会浪费真机回合）

1. **先探针后主路**：任何 D3D/MF 实验必须用一次性探针验证，**绝不在主 reader 上直接试**（0.2.5.34 教训：整条视频丢失）。
2. **上游改动必须 `#if defined(WK_WINUWP)` 守卫 + `Apotheosis:` 注释**。
3. **改完 `WebKit/` 必须重新生成补丁**：
   ```powershell
   cd E:\Apotheosis\WebKit
   git add -N -- <新文件>
   git diff HEAD -- <7个文件清单> > E:\Apotheosis\patches\2026-10-09-winuwp-media-mf.patch
   git reset -q -- <新文件>
   ```
   `WebKit/` **不在仓库**，未生成补丁的改动会丢。
4. **改 `port/` 或 `harness/` 必须升 `harness/Package.appxmanifest` 版本号**（同版重装是静默 no-op，CI 会拦）。
5. **C ABI 两份头必须同步**：`port/WebCoreDriver.h` 与 `harness/WebCoreDriver.h` 逐字节一致（CI 会拦）。
6. **本机 16GB，`ninja -j 4` 会 OOM** → 用 `-j 2`。全量 WebCore ≈ 1h45m；harness-only ≈ 1–2 分钟。
7. **MSBuild 前清 `*_PROXY`**（`NO_PROXY`/`no_proxy` 大小写重复会炸 `MSB6001`）。

---

## 6. 构建与部署（照抄）

```powershell
# 改了 WebKit 引擎源 → 全链
. E:\Apotheosis\port\arm32-uwp-env.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -j 2 -C E:\Apotheosis\build-clang-gpu WebCore
pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1
pwsh -File E:\Apotheosis\port\build-harness.ps1

# 只改 harness → 1–2 分钟
pwsh -File E:\Apotheosis\port\build-harness.ps1

# 部署（设备在线时自动装 + 启动）
pwsh -File E:\Apotheosis\tools\_deploy-curl.ps1 -Ver <版本号> -WaitMinutes 90
```

**真机测试流程**（唯一可靠的）：
设备亮屏 → 打开 app → 地址栏输入 `http://192.168.3.108:8099/values.html` → 等 15 秒 → 读证据。
PC 侧测试服务器：`pwsh -File E:\Apotheosis\tools\Test-MediaServer.ps1 -Port 8099`（根目录 `port\_mediatest`）。

---

## 7. 证据与判据怎么读

```powershell
# 音频/图生命周期 + 端点 + 崩溃
pwsh -NoProfile -File E:\Apotheosis\tools\_pull-audio-diag.ps1
# 完整证据（stage.txt / console.txt / crash.txt）
pwsh -NoProfile -File E:\Apotheosis\tools\_pull-evidence.ps1
```

**`media` 行关键字段**：

| 字段 | 含义 |
|---|---|
| `readers=` | SourceReader 是否建成（0 = 创建失败） |
| `frames=解码/呈现` | 视频帧 |
| `pcm=推/取/丢` | 音频帧 |
| `q=深度@采样率/声道` | 音频环 |
| `dxva=` | `sw` = 软解（当前） |
| `dxvaprof=` / `dxvafl=` / `h264=` | 设备能力（真机：10 / 0xB000 / 1） |
| `err=` | MF HRESULT |
| `phase=` / `cpush=` | 解码阶段 / 呈现次数 |

> ★ **量子节奏判据**（本项目最可靠的"是否真接上设备"标尺）：
> 真实设备时钟 ≈ **100 量子/秒**（100 × 10ms）；**~1500/秒 = 空转，没接设备**。

---

## 8. 交付要求

1. **支线 A 结论**：`SetMultithreadProtected` 是否为缺失拼图（真机证据），以及若否，`MediaPlayer` 帧服务路线是否可行。
2. **支线 B 结论**：`AdaptiveMediaSource` 能否在 App Container 内吃 B站 DASH（真机证据）。
3. **不回归**：软解播放 + 音频（`AudioDeviceOutputNode`）必须照常工作。
4. **文档**：结论追加到 `docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md`（新章节）+ `docs/MEDIA-MF-IMPLEMENTATION.md`（设备回合）。
5. **补丁**：改了 `WebKit/` 就重新生成 `patches/2026-10-09-winuwp-media-mf.patch`。
6. **真机读数**：任何"能做/不能做"的结论都必须附真机 `media` 行或 crash 证据——**不要用文档推测代替真机**（本项目为此绕过好几次弯路）。

---

## 9. 反面教材清单（别重犯）

| 事故 | 教训 |
|---|---|
| 0.2.5.34 给主 reader 设 `D3D_MANAGER` + `ENABLE_VIDEO_PROCESSING` → `E_FAIL`，视频全丢 | **先探针**；主路只接受已验证配置 |
| "DXVA 此路不通"（§12.7） | **少了 `SetMultithreadProtected`** —— 结论下太早 |
| "AudioGraph 在此设备不可用"（§11.7） | 错。真实根因是**漏建 `AudioDeviceOutputNode`**；`pcm` 计数正常 ≠ 到达设备 |
| 0.2.5.33 空转自停在 `QuantumStarted` 回调内拆图 | **绝不在回调内拆图**（`Windows.Media.Audio.dll` 内部崩），必须 dispatcher 派发 |
| `AudioLog` 里 `ApplicationData::Current->LocalFolder` 无判空 | 生命周期边缘为 null，**SEH 崩（`/EHs-c-` 下 try/catch 兜不住）**，必须显式判空 |
