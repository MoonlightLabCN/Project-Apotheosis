# Project Apotheosis / EdgeHTML Reborn

**把现代 WebKit/WebCore 搬到被微软放弃的 Windows 10 Mobile 上——让 Lumia 950（ARM32 / UWP / App Container）跑真实现代网页，带 JIT 与 GPU 合成。**

[![CI](https://github.com/MoonlightLabCN/Project-Apotheosis/actions/workflows/ci.yml/badge.svg)](https://github.com/MoonlightLabCN/Project-Apotheosis/actions/workflows/ci.yml)

本仓库只包含**移植层与宿主**（`port/` + `harness/` + 构建/部署工具 + 文档 + 上游补丁）。
GB 级的上游 WebKit 源码树**不在仓库内**（见 [仓库布局](#仓库布局--什么被跟踪)）。

---

## 真机现状（Lumia 950, Win10M 15254）

以下全部为**真机验证**结论，不是构建机推断：

| 能力 | 状态 |
|---|---|
| WTF + JavaScriptCore（LLInt 解释执行） | ✅ 真机跑通 |
| WebCore + Cairo 软渲染（Bing / GitHub / Apple 等真实站点） | ✅ 真机跑通 |
| 真实事件交互（点击/滚动/输入/选择） | ✅ 真机跑通 |
| ANGLE(D3D11 FL9_3) + TextureMapper GPU 合成直呈现 SwapChainPanel | ✅ 真机跑通，平滑滚动 + 捏合缩放 |
| `<video>` H.264 软解（MF SourceReader → Video Processor → RGB32 → 合成呈现） | ✅ 真机确认"画面能动"，播完整片 |
| 硬件视频解码（DXVA） | ❌ **已结论：此路不通**——App Container 内无法把 D3D 设备交给 MF 解码器（设备侧有 FL 11_0 + 10 个解码 profile，但 `MF_SOURCE_READER_D3D_MANAGER` 必失败，无优雅退路）。推导全过程见 `docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md` §12 |
| `<video>`/`<audio>` 声音输出 | 🚧 进行中——端点存在（真机枚举到 2 个 AudioRender 设备），AudioGraph 接线问题排查中 |
| MSE / Web Audio / WebRTC | ⛔ 未开始（`supportsType()` 对非 FileOrHLS 直接拒绝，快速失败而非挂住） |
| Lumia 920/930（WDDM 1.x） | ✅ **已适配**：设备身份门自动降级 Cairo 软件渲染（ANGLE 在这些机器上会崩，详见 §13） |

## 架构

三层经 C ABI 解耦：

```
Harness (C++/CX UWP, MSVC v143)              harness/
  · MainPage: 地址栏/工具栏 + 触摸手势 → 引擎滚动/点击/缩放/选择
  · GpuPanel (SwapChainPanel) ← GPU 直呈现 | RenderImage (WriteableBitmap) ← 软件回退
        │  C ABI = WebCoreDriver.h  (extern "C")
WebCoreDriver (port/, clang-cl → WebCoreDriver-gpu.lib)
  · 常驻 Page/Frame/FrameView 会话、真实事件派发、链接提取
  · 两条呈现路：Cairo paintToRGBA（软件）| TextureMapper→ANGLE swapchain（GPU）
  · PortChromeClient / LoadingFrameLoaderClient / Port*Strategies / 媒体后端
        │
WebCore / JavaScriptCore / WTF (clang-cl, thumbv7-windows-msvc, App Container)
  · ARM32 / App Container 补丁全部 WK_WINUWP 守卫；上游源不在本仓库
```

**线程铁律**：present 只在引擎线程；UI 线程绝不同步 wait 引擎；所有 C ABI 调用在单一引擎线程串行化。

## 仓库布局 / 什么被跟踪

**跟踪**：移植层与宿主——`port/`（驱动 + stub + 构建脚本）、`harness/`（UWP App）、`tools/`（部署/诊断）、`docs/`（交接单/实现记录）、`patches/`（上游补丁）、`angle/include`（ANGLE 头）。

**不跟踪**（可重下/可重建，GB 级）：`WebKit/`（sparse webkitgtk-2.52.4，独立 git repo）、`build-clang-*/`、`build-release/`、`deps-build/`、字体、`*.log`、`*.pfx`。

> ⚠️ **ASCII 路径铁律**：构建链（Ruby 代码生成器 / meson）对非 ASCII 路径敏感——仓库必须在 `E:\Apotheosis`，vcpkg 在 `C:\vcpkg`。CI 里有路径检查守着这条。

## 硬约束（违反必踩坑，详见 [AGENTS.md](AGENTS.md)）

- **三套工具链并存，别混**：引擎 = clang-cl（`--target=thumbv7-unknown-windows-msvc`）；移植驱动 = clang-cl + lld-link；harness = MSVC v143 ARM + SDK 22621 arm 库（`port\arm32-uwp-env.ps1` 手搓环境）。
- **C++ 异常必须关**：`_HAS_EXCEPTIONS=0` + `/EHs-c-`（clang 的 thumbv7-windows-msvc 无法 lower `cleanupret`）。
- **所有上游 WebKit 改动用 `#if defined(WK_WINUWP)` 守卫 + `Apotheosis:` 注释**；改完 `WebKit/` **必须重新生成 `patches/2026-10-09-winuwp-media-mf.patch` 再提交**（`WebKit/` 不在仓库，未提交的改动会丢）。
- **软件渲染是通用底座，GPU 运行时切换**：合成开关严格 gate 在 `g_gpuActive`（仅 `WebCoreGpuInit` 成功后置 true）；GPU 未起时回 Cairo 软渲染，零回归。
- **C ABI 有两份副本**：`port/WebCoreDriver.h` 与 `harness/WebCoreDriver.h`，**必须逐字节一致**（CI 有检查）。
- **同版重装是静默 no-op**：改 harness/driver 必须升 `harness/Package.appxmanifest` 的 Version，否则设备继续跑旧二进制（CI 有检查）。

## 构建

```powershell
# 改 port/*.cpp 驱动后：重编 + 重链 GPU 驱动
pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1

# 改 harness 后：构建 ARM32 UWP appx（XAML 改了先跑 gen-xaml-codebehind.ps1）
pwsh -File E:\Apotheosis\port\build-harness.ps1

# 改了上游 WebKit 源（WK_WINUWP 补丁）后：增量重编引擎再重链驱动
. E:\Apotheosis\port\arm32-uwp-env.ps1
& "…\Ninja\ninja.exe" -C E:\Apotheosis\build-clang-gpu WebCore
pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1
```

> 本机 16GB 上 `ninja -j 4` 会 OOM（unified sources 并行编译），用 `-j 2`；全量 WebCore ≈ 1h45m。
> 详细构建说明（含手搓 XAML 工具链的来龙去脉）见 [docs/HARNESS-BUILD.md](docs/HARNESS-BUILD.md)。

## 部署到真机

前置：设备开机、同一 WiFi、设置→面向开发人员→**Device Portal** 开启；appx 依赖 `Microsoft.VCLibs.140.00 (ARM)`。

```powershell
# 安装并启动（-Ver 必须与 Package.appxmanifest 一致）
pwsh -File E:\Apotheosis\tools\install-only.ps1 -Ip <设备IP> -Ver <版本号>

# 弱网/设备易睡环境用容错部署（流式上传 + 重试 + 落盘轮询）
pwsh -File E:\Apotheosis\tools\_deploy-curl.ps1 -Ver <版本号>

# 媒体/渲染真机回合（LAN 测试服务器 + 证据回抓）
pwsh -File E:\Apotheosis\tools\Test-MediaServer.ps1 -Port 8099   # PC 侧常驻
pwsh -File E:\Apotheosis\tools\_media-round-curl.ps1            # 设备侧一轮 + 拉 stage.txt
```

**这是 x64 构建机，ARM32 appx 跑不了——引擎验证唯一靠真机。**

## 文档地图

| 文档 | 内容 |
|---|---|
| [AGENTS.md](AGENTS.md) / [CLAUDE.md](CLAUDE.md) | 完整工程约束（先读这个） |
| [PROJECT-OVERVIEW.md](PROJECT-OVERVIEW.md) | 仓库现状与架构总览 |
| [docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md](docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md) | **媒体线交接单**（三道门/根因链/硬件解码终判/920-930 适配/每轮真机读数） |
| [docs/MEDIA-MF-IMPLEMENTATION.md](docs/MEDIA-MF-IMPLEMENTATION.md) | 媒体线按"设备回合 1..6"的完整推导记录 |
| [docs/HARNESS-BUILD.md](docs/HARNESS-BUILD.md) | 构建体系（含手搓 XAML 工具链） |
| [WK_WINUWP-PATCH.md](WK_WINUWP-PATCH.md) + [patches/](patches/) | 上游补丁说明与补丁本体 |
| [CHANGELOG.md](CHANGELOG.md) | 版本历史 |

## 分支

| 分支 | 状态 |
|---|---|
| `media-mf` | **当前开发线**（媒体/DXVA/920-930 适配/工具链） |
| `gpu-path1` | 存档（GPU 合成开发线的历史，远程保留） |
| `master` | 存档（纯 JIT 封存线，远程保留） |

## 许可

移植层与宿主见 [LICENSE](LICENSE) / [NOTICE](NOTICE)；上游 WebKit 遵循其自身许可（LGPL/BSD 双轨）。
