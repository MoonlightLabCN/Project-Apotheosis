# Project Apotheosis / EdgeHTML Reborn

**把现代 WebKit/WebCore 移植到被微软放弃的 Windows 10 Mobile（ARM32 / UWP / App Container），让 Lumia 950 等设备运行真实的现代网页。**

[![CI](https://github.com/MoonlightLabCN/Project-Apotheosis/actions/workflows/ci.yml/badge.svg?branch=media-mf)](https://github.com/MoonlightLabCN/Project-Apotheosis/actions/workflows/ci.yml)

- **当前开发分支**：`media-mf`
- **当前 App 版本**：`0.8.0.0`（`harness/Package.appxmanifest`，最终发布准备中）
- **当前补丁基线**：`webkitgtk-2.54.0`（早期开发线为 `2.52.4`）
- **目标设备**：Lumia 950 / 950 XL（Windows 10 Mobile 15254，ARM32）
- **仓库性质**：只包含**移植层与宿主**；GB 级上游 WebKit 源码树不在仓库内
- **发布状态**：`0.8.0.0` 最终发布准备中；发布后重点转向**视频/音频**与**体验优化**

---

## 目录

1. [项目简介](#1-项目简介)
2. [技术背景](#2-技术背景windows-10-mobile--arm32--uwp)
3. [架构](#3-架构)
4. [已验证能力](#4-已验证能力)
5. [仓库布局](#5-仓库布局)
6. [构建](#6-构建)
7. [部署到真机](#7-部署到真机)
8. [测试](#8-测试)
9. [文档地图](#9-文档地图)
10. [分支策略](#10-分支策略)
11. [参与贡献](#11-参与贡献)
12. [许可证](#12-许可证)
13. [致谢](#13-致谢)
14. [English Summary](#english-summary)

---

## 1. 项目简介

**Project Apotheosis（EdgeHTML Reborn）** 是一个研究性开源项目：把现代
**WebKit / WebCore** 移植到 **Windows 10 Mobile（ARM32 / UWP / App Container）**。

它不是一个"套壳浏览器"：项目重新编译了 WTF、JavaScriptCore、WebCore，并为
32 位 ARM、App Container 沙箱、ANGLE/D3D11 图形栈与 Media Foundation 媒体栈
写了完整的平台适配层。宿主是一个 C++/CX UWP 应用，通过稳定的 C ABI 与引擎
驱动解耦。

**目标**：让一台 2015 年的 Windows Phone 能打开 2026 年的真实网站，
并尽可能保留现代 WebKit 的语义（DOM/CSS/JS/网络/媒体）。

---

## 2. 技术背景：Windows 10 Mobile / ARM32 / UWP

Windows 10 Mobile 是一个已经停止支持的 ARM32 移动操作系统。它的应用运行在
UWP **App Container** 中，与桌面 Win32 相比有一系列硬约束：

| 约束 | 影响 |
|---|---|
| 只能链接 UWP 伞库 `WindowsApp.lib` | 大量桌面 API（user32/gdi32/advapi32 等）不可直接使用 |
| 文件系统 / 注册表 / 剪贴板 / 证书库被沙箱限制 | 需要 WinRT 或应用 `LocalState` 替代 |
| 32 位 ARM（Thumb-2）工具链逐步被移除 | 需要固定 MSVC v143 14.44 + SDK 22621，并手搓 ARM32 环境 |
| 图形栈依赖 ANGLE / D3D11 | 老设备（WDDM 1.x）驱动兼容性差，需要运行时降级 |
| 媒体只支持 Media Foundation 的 UWP 子集 | 硬件解码（DXVA）在 App Container 内不可用 |

Apotheosis 的工作，就是在这些约束下把现代 WebKit 逐层裁剪、适配、重新链接。

---

## 3. 架构

```
┌─────────────────────────────────────────────────────────────┐
│ harness/  C++/CX UWP 宿主（MSVC v143 ARM, SDK 22621）        │
│  · MainPage: 地址栏/工具栏 + 触摸手势                        │
│  · GpuPanel (SwapChainPanel) ← GPU 直呈现                    │
│  · RenderImage (WriteableBitmap) ← 软件回退                  │
└───────────────────────────┬─────────────────────────────────┘
                            │  C ABI = WebCoreDriver.h
┌───────────────────────────▼─────────────────────────────────┐
│ port/  WebCoreDriver（clang-cl + lld-link）                  │
│  · 常驻 Page/Frame/FrameView 会话、真实事件派发              │
│  · 两条呈现路：Cairo paintToRGBA（软件）                     │
│                TextureMapper → ANGLE swapchain（GPU）        │
│  · PortChromeClient / LoadingFrameLoaderClient / 媒体后端    │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│ WebKit/  WTF + JavaScriptCore + WebCore                      │
│  · clang-cl, --target=thumbv7-unknown-windows-msvc           │
│  · 所有改动以 #if defined(WK_WINUWP) 守卫 + Apotheosis: 注释 │
└─────────────────────────────────────────────────────────────┘
```

**线程铁律**：present 只在引擎线程；UI 线程绝不同步等待引擎；所有 C ABI 调用在
单一引擎线程串行化。

**两份 C ABI 头**：`port/WebCoreDriver.h` 与 `harness/WebCoreDriver.h` 必须逐字节
一致（CI 强制）。

---

## 4. 已验证能力

> 下表只列**可核实**的结论。标记：✅ 真机验证　🚧 实验/进行中　⛔ 未开始或已证伪。

| 能力 | 状态 | 说明 |
|---|---|---|
| WTF + JavaScriptCore（LLInt 解释执行） | ✅ | Lumia 950 / Win10M 15254 真机跑通 |
| WebCore + Cairo 软件渲染 | ✅ | Bing / GitHub / Apple 等真实站点 |
| 真实事件交互（点击/滚动/输入/选择） | ✅ | 引擎侧真实事件派发 |
| ANGLE（D3D11 FL9_3）+ TextureMapper GPU 合成 | ✅ | 直呈现 `SwapChainPanel`，平滑滚动 + 捏合缩放 |
| Lumia 920/930（WDDM 1.x）适配 | ✅ | 设备身份门自动降级 Cairo 软件渲染 |
| `<video>` H.264 软解（MF SourceReader → RGB32 → 合成） | ✅ | 真机确认画面可播放 |
| 硬件视频解码（DXVA） | ⛔ | 已结论：App Container 内无法把 D3D 设备交给 MF 解码器 |
| `<video>` / `<audio>` 声音输出 | 🚧 | 端点可枚举；AudioGraph 接线未完成 |
| MSE / Web Audio / WebRTC | ⛔ | 未开始（`supportsType()` 对非 FileOrHLS 快速失败） |
| JavaScriptCore JIT | 🚧 | **实验性**：可执行内存原语（W^X / codeGeneration）已在真机验证；JIT 本体**从未在出货构建中启用**（2026-09-27 订正，见 `docs/JIT-254-PORTING.md`） |
| ARMv7 / Thumb-2 汇编器与 offlineasm | ✅ | Windows COFF、icache flush、LLInt PC 寄存器等 |
| WinRT / AppContainer API 替换 | ✅ | 文件、证书、线程、计时器等 |
| libcurl / OpenSSL 网络与 TLS | ✅ | 打包 CA、SameSite、WebSocket |
| Public Suffix List（blob 注入） | ✅ | `fetch-publicsuffix.ps1` 按 SHA 钉死 |
| 存储（Cookie/IndexedDB/Storage） | ✅ | JSON Lines + 应用 `LocalState` |
| 页内查找 / 标签页 / 书签 / 下载 / 设置 | ✅ | 宿主 UI |
| 中/英/俄 UI | ✅ | 本地化资源 |
| Media Foundation 适配 | 🚧 | SourceReader 软解可用；硬件路已证伪 |

---

## 5. 仓库布局

**跟踪**（本仓库可审查的源码）：

| 路径 | 内容 |
|---|---|
| `port/` | WebCore 驱动、Port 层客户端、stub、构建/链接脚本 |
| `harness/` | C++/CX UWP 宿主、XAML、appx 清单 |
| `tools/` | Device Portal 部署 / 诊断 / 证据回抓脚本 |
| `tests/` | PSL 主机单测 + Web 平台设备测试页 |
| `jittest/` | JIT 探针宿主 |
| `docs/` | 交接单、实现记录、构建指南 |
| `patches/` | 上游补丁（增量） |
| `angle/include/` | ANGLE 头文件 |
| `wk-winuwp.patch` | WebKit 完整补丁（相对 2.54.0） |

**不跟踪**（可重下/可重建）：

- `WebKit/`（独立 git 仓库，GB 级）
- `build-clang-*/`、`build-release/`、`deps-build/`
- 字体、ANGLE 二进制、`*.log`、`*.pfx`、`*.appx`

> **ASCII 路径铁律**：构建链（Ruby 代码生成器 / meson）对非 ASCII 路径敏感——
> 仓库必须在 `E:\Apotheosis`，vcpkg 在 `C:\vcpkg`。CI 有路径检查。

---

## 6. 构建

### 6.1 三套工具链（不能混用）

| 层 | 工具链 | 产物 |
|---|---|---|
| WTF / JSC / WebCore | clang-cl，`--target=thumbv7-unknown-windows-msvc` | WebKit ARM32 引擎 |
| `port/*.cpp` | clang-cl + lld-link | `WebCoreDriver-gpu.lib/.dll` |
| `harness/` XAML codegen | VS2017 MSBuild + v141 ARM + SDK 17763 | 官方 C++/CX XAML `.g.*` / XBF |
| `harness/` final build | VS18 MSBuild + MSVC v143 ARM 14.44.35207 + SDK 22621 | 宿主 + appx |

### 6.2 入口

```powershell
# 1) 准备工具链环境（ARM32 UWP）
. .\port\arm32-uwp-env.ps1

# 2) 配置引擎（首次）
pwsh -File .\port\configure-gpu.ps1

# 3) 增量编译引擎
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" `
  -j 2 -C E:\Apotheosis\build-clang-gpu WebCore

# 4) 重链驱动
pwsh -File .\port\link-driver-gpu.ps1

# 5) 打包 appx（改了 XAML 先跑 gen-xaml-codebehind.ps1）
pwsh -File .\port\build-harness.ps1
```

> 本机 16 GB 内存下 `ninja -j 4` 会 OOM（unified sources 并行编译），请用 `-j 2`；
> 全量 WebCore 约 1 小时 45 分钟。详细说明见 `docs/HARNESS-BUILD.md`。

### 6.3 关键约束

- **C++ 异常必须关**：`_HAS_EXCEPTIONS=0` + `/EHs-c-`（clang 的 thumbv7-windows-msvc
  无法 lower `cleanupret`）。
- **所有上游 WebKit 改动用 `#if defined(WK_WINUWP)` 守卫 + `Apotheosis:` 注释**。
- **改完 `WebKit/` 必须重新生成 `patches/2026-10-09-winuwp-media-mf.patch` 再提交**
  （`WebKit/` 不在仓库，未提交的改动会丢）。
- **同版重装是静默 no-op**：改 harness/driver 必须升 `Package.appxmanifest` 的 Version。

---

## 7. 部署到真机

前置：设备开机、同一 WiFi、设置 → 面向开发人员 → **Device Portal** 开启；
appx 依赖 `Microsoft.VCLibs.140.00 (ARM)`。

```powershell
# 安装并启动（-Ver 必须与 Package.appxmanifest 一致）
pwsh -File .\tools\install-only.ps1 -Ip <设备IP> -Ver <版本号>

# 弱网/设备易睡环境用容错部署（流式上传 + 重试 + 落盘轮询）
pwsh -File .\tools\_deploy-curl.ps1 -Ver <版本号>

# 媒体/渲染真机回合（PC 侧常驻服务器 + 设备侧一轮 + 证据回抓）
pwsh -File .\tools\Test-MediaServer.ps1 -Port 8099
pwsh -File .\tools\_media-round-curl.ps1
```

> **这是 x64 构建机，ARM32 appx 跑不了——引擎验证唯一靠真机。**

---

## 8. 测试

```powershell
# 主机 PSL 单测（x64，秒级）
pwsh -File .\tests\psl\run-psl-tests.ps1

# 设备 Web 平台测试（真机手动打开）
python .\tests\web-platform\server.py
# 然后在 Lumia 上打开 http://<构建机 LAN IP>:8099/
```

CI 还守着一批"踩过才知道"的纪律：C ABI 两份头一致、路径全 ASCII、补丁结构完整、
`.ps1` 语法、版本号必须升、模板/文档存在性等。详见 `.github/workflows/ci.yml`。

---

## 9. 文档地图

| 文档 | 内容 |
|---|---|
| [`AGENTS.md`](AGENTS.md) / [`CLAUDE.md`](CLAUDE.md) | 完整工程约束（先读这个） |
| [`PROJECT-OVERVIEW.md`](PROJECT-OVERVIEW.md) | 架构与仓库总览 |
| [`docs/CAPABILITIES.md`](docs/CAPABILITIES.md) | 能力/接缝/风险面清单 |
| [`docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md`](docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md) | 媒体线交接单（根因链 / 硬解终判 / 920-930 适配） |
| [`docs/MEDIA-MF-IMPLEMENTATION.md`](docs/MEDIA-MF-IMPLEMENTATION.md) | 媒体线设备回合记录 |
| [`docs/JIT-254-PORTING.md`](docs/JIT-254-PORTING.md) | 2.54 JIT 适配现状 |
| [`docs/JIT-ARM32-R9-ROOT-CAUSE-2026-10-07.md`](docs/JIT-ARM32-R9-ROOT-CAUSE-2026-10-07.md) | ARM32 PutByVal 寄存器冲突根因 |
| [`docs/HARNESS-BUILD.md`](docs/HARNESS-BUILD.md) | 构建体系（含手搓 XAML 工具链） |
| [`docs/TILEGRID-DESIGN.md`](docs/TILEGRID-DESIGN.md) | TileGrid v2 设计 |
| [`docs/WP81-FEASIBILITY.md`](docs/WP81-FEASIBILITY.md) | WP8.1 可行性研究 |
| [`WK_WINUWP-PATCH.md`](WK_WINUWP-PATCH.md) + [`patches/`](patches/) | 上游补丁说明与补丁本体 |
| [`CHANGELOG.md`](CHANGELOG.md) | 版本历史 |

---

## 10. 分支策略

| 分支 | 角色 | 状态 |
|---|---|---|
| `media-mf` | **主开发线**（默认分支） | 活跃 |
| `gpu-path1` | GPU 合成开发线历史 | 存档（只读） |
| `master` | 纯 JIT 封存线 | 存档（只读） |

- 新功能 / 修复请基于 `media-mf` 开分支，PR 目标分支为 `media-mf`。
- 存档分支不接受新提交，仅保留历史。

---

## 11. 参与贡献

欢迎 Issue 与 PR。请先阅读：

- [`CONTRIBUTING.md`](CONTRIBUTING.md) —— 分支、提交、证据与验证要求
- [`AGENTS.md`](AGENTS.md) —— 工程铁律（工具链 / ASCII 路径 / 异常 / 版本纪律）
- [`.github/PULL_REQUEST_TEMPLATE.md`](.github/PULL_REQUEST_TEMPLATE.md) —— PR 检查清单

**提 Issue 前**：请附上真机证据（`stage.txt` / `crash.txt` / 服务器日志），
没有证据的缺陷报告很难定位。

**提 PR 前**：确认目标分支是 `media-mf`、版本号已升、C ABI 两份头一致、
补丁已重新生成、`.ps1` 语法通过。

---

## 12. 许可证

- **本项目原创代码**：MIT License，全文见 [`LICENSE`](LICENSE)。
- **第三方组件**：各自保留原始许可证，详见 [`NOTICE`](NOTICE)：
  - WebKit / WebCore / JavaScriptCore / WTF —— LGPL-2.1 / BSD-2-Clause
  - ANGLE 头（`angle/include/`）—— Khronos / MIT 式
  - Cairo —— LGPL-2.1 / MPL-1.1
  - ICU / libcurl / FreeType / HarfBuzz —— 各自许可
- 上游 WebKit 源码不随本仓库分发；补丁基线见 `WK_WINUWP-PATCH.md`。

---

## 13. 致谢

- **Jimmyxiao2009** —— 项目发起人与主要维护者：WebKit 移植、JSC/JIT 适配、
  GPU 合成、媒体播放与整体架构。
- **Andreas Horn** —— 主要贡献者：TileGrid v2、补丁再生成、构建体系与交互功能。
- 所有提交过 Issue、PR、真机测试与文档改进的贡献者。

---

## English Summary

**Project Apotheosis (EdgeHTML Reborn)** ports modern **WebKit/WebCore** to
**Windows 10 Mobile (ARM32 / UWP / App Container)**, targeting devices such as
the Lumia 950 / 950 XL (Windows 10 Mobile 15254).

- **Active branch**: `media-mf` (default) · **App version**: `0.8.0.0` (final release prep)
- **WebKit baseline**: `webkitgtk-2.54.0` (early lines used `2.52.4`)
- **Repository scope**: port layer + UWP host + tooling + patches only; the
  multi-gigabyte WebKit tree is not included.

**Architecture**: a C++/CX UWP host talks to a `WebCoreDriver` over a stable
C ABI; the driver owns a persistent Page/Frame session and can present either
through Cairo (software) or TextureMapper + ANGLE/D3D11 (GPU, `SwapChainPanel`).

**Verified capabilities**: WTF + JavaScriptCore (LLInt), Cairo rendering of real
sites, real event dispatch, GPU compositing on Lumia 950, 920/930 WDDM 1.x
fallback, H.264 software video decode. **Experimental / not shipped**: JSC JIT.
**Not started**: MSE, Web Audio, WebRTC. **Ruled out**: DXVA hardware decode in
App Container.

**Build**: three toolchains (clang-cl for WebKit, clang-cl + lld-link for the
driver, MSVC v143 ARM + SDK 22621 for the host). Entry point:
`pwsh -File port/build-harness.ps1`. Runtime validation requires a real device.

**Contributing**: see [`CONTRIBUTING.md`](CONTRIBUTING.md) and
[`AGENTS.md`](AGENTS.md). PRs target `media-mf`.

**License**: MIT for original code; third-party components keep their own
licenses (see [`NOTICE`](NOTICE)).
