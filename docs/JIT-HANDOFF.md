# JIT 交接单（给接手 JSC JIT 的 agent）

> 2026-09-27 写。目标：把 WebKit 2.54 的 JSC baseline JIT 在 ARM32 UWP（Lumia 950,
> Win10M 15254, App Container）上编译通过并在真机跑起来。当前状态：**门控已接好、
> 从未启用过、撞过第一波编译墙**。本文是工作单；权威状态以
> `docs/JIT-254-PORTING.md` 为准（它记到 2026-09-26），本文补上 09-27 的构建环境结论。

## 1. 先读这些（按顺序）

1. `AGENTS.md` —— 仓库铁律（ASCII 路径、三套工具链、异常必须关、所有上游改动
   `#if defined(WK_WINUWP)` + `Apotheosis:` 注释、单引擎线程铁律）。
2. `docs/JIT-254-PORTING.md` —— JIT 适配现状：已落地的 7 个编译期修复、启用步骤、
   已知的撞墙顺序。**你要从它的“启用前仍需完成”清单往下推。**
3. `docs/CAPABILITIES.md` §2.7 —— JIT 是“显式关闭”，不是 bug。

## 2. 起点事实（都已验证，别重新查）

- `WTF/wtf/PlatformEnable.h:729-741`：JSVALUE32_64 + ARM_THUMB2 下，未定义
  `WK_WINUWP_JIT` 时**强制** `ENABLE_JIT 0`（cmakeconfig.h 里的 `ENABLE_JIT=ON`
  会被 undef 掉）。定义即保留 cmake 的值 → JIT 真开。
- 门控链路已接好：`OptionsWinUWP.cmake`（`APOTHEOSIS_JIT → -DWK_WINUWP_JIT=1`）、
  `port/configure-gpu.ps1`（当前 OFF）、`port/compile-driver-gpu.ps1`（驱动侧同步）、
  `port/WebCoreDriver.cpp` 的 jit-pool 行已 `#if ENABLE(JIT)` 守卫。
- **历史上三条线（build-clang-gpu / build-clang-jit / build-clang-webcore）的
  `JavaScriptCore.lib` 里 `startOfFixedExecutableMemoryPoolImpl` 计数都是 0**——
  JIT 从未在任何出货构建里启用过。你的工作就是让这个计数 > 0。
- 运行时前提已验证过，不用重做：`harness/JitProbe.cpp`（Thumb 机器码三场景：
  RW→RX / 直接 RWX / RW→RWX）+ `Package.appxmanifest` 的 `codeGeneration`
  普通 Capability + `WTF/wtf/win/OSAllocatorWin.cpp` 的 W^X 翻转
  （VirtualAllocFromApp RW + VirtualProtectFromApp → PAGE_EXECUTE_READ）。
  参考实现：`H:\项目\UWP\Project-Chronos-Collapse`（tinycc 的 JIT，loader 在
  `SandboxProbe/tccmod_loader.c` / `Phase3Probe/Loader/tccmod_loader.cpp`；
  注意路径非 ASCII，只读参考，别在那里面构建）。
- JSC ARM32 可执行池 16MB（开 JUMP_ISLANDS 32MB），提示地址走
  `Options::jitMemoryReservationAddress()` **默认 0 = 无提示**，与 App Container
  “VirtualAllocFromApp 不能指定基址”不冲突。

## 3. 怎么开（复现步骤）

```powershell
# 1) configure-gpu.ps1 里把 -DAPOTHEOSIS_JIT 改 ON
pwsh -File E:\Apotheosis\port\configure-gpu.ps1
# 2) 引擎增量重编（ninja 断点续跑，可反复调用直到绿）
. E:\Apotheosis\port\arm32-uwp-env.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -j 4 -C E:\Apotheosis\build-clang-gpu WebCore
# 3) 符号验收：startOfFixedExecutableMemoryPoolImpl 计数 > 0
& "C:\Program Files\LLVM\bin\llvm-nm.exe" E:\Apotheosis\build-clang-gpu\lib\JavaScriptCore.lib | findstr /C:"startOfFixedExecutableMemoryPoolImpl"
# 4) 驱动重链 + harness 重打
pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1
pwsh -File E:\Apotheosis\port\build-harness.ps1
```

**构建环境教训（09-27 实测，照做可省半天）**：

- **ninja 并行度用 `-j 4`**。本机 12 核 / 16GB，`-j12` 编 WebCore 大 unity TU 会把
  clang frontend 打到 OOM（`clang frontend command failed due to signal`，一次挂 6 个
  TU）。`-j4` 全量 1006 步稳过。
- 全量重建是常态（改任何 WTF 头 → 上千 TU 重编），别试图躲。
- dcce1 远端构建机（tailscale 名 `desktop-dcce212558a843ed-20260806111728416-1`）
  SSH 可达但**所有本地密钥被拒**（机器重装过），本机出产物即可。

## 4. 已知撞墙清单（从 JIT-254-PORTING.md 继承，未动）

按文档记录的撞墙顺序：

1. **`wtf/Variant.h` 的 std::variant 替换 vs 2.54 的 `switchOn`**：部分 JIT TU 实例化
   `std::visit(WTF::Visitor<...>)` 报 "no matching function for call to 'invoke'"
   （MSVC STL variant 1564）。mpark::visit 与 std::visit 的可调用判定有差异，
   `WTF::Visitor` 重载集需要适配层或泛型兜底。**WTF 核心模板，改动要最小、
   要 `#if defined(WK_WINUWP)` 守卫或做成两个后端都兼容的写法。**
2. `JITInlineCacheGenerator.h` 还差 `DFG::UnlinkedPropertyInlineCache` 完整类型
   （浅，同修法 3/4）。
3. 后续批次未知（2.54 给 baseline JIT 加了不少 64 位惯用法）。每磨完一批就把
   新的墙补进 `docs/JIT-254-PORTING.md` 的清单。

已落地、启用后仍保留的 7 个修复见该文档“已落地的 JIT 编译期修复”节，别回退。

## 5. 验收标准（DoD）

1. `JavaScriptCore.lib` 里 `startOfFixedExecutableMemoryPoolImpl` 计数 > 0。
2. driver 的 jit-pool 行不再是 `(LLInt build, no JIT pool)`。
3. harness appx 构建 exit 0，`Harness.exe` 链接无未解析符号。
4. **真机回合**（`tools\deploy-launch.ps1`，默认版本号已对齐 0.2.0.0）：跑一个
   JS 重页（或 `tests/web-platform` 里的 JS 页），看 `LocalState\crash.txt` 无
   fastfail、`perf.csv` 有渲染行、页面交互正常；有条件就跑
   `harness/JitProbe.cpp` 同链路证明 JIT 页可执行。
5. 若最终没磨完：**把 configure 默认留 OFF**，在 `docs/JIT-254-PORTING.md` 记录
   卡点，别把半成品留在默认路径上。

## 6. 纪律（和这个仓库其他工作一样）

- 上游 WebKit 树在 `E:\Apotheosis\WebKit`（gitignore，改动不进 git），所有改动
  `#if defined(WK_WINUWP)` + `Apotheosis:` 注释。
- `wk-winuwp.patch` 工件与树 md5 一致的声明目前**已经失效**（09-27 改了 6 个 WTF
  文件未重生成）——你磨完 JIT 后如果手上有 pristine 上游基线，一并重生成，
  `WK_WINUWP-PATCH.md` 也要同步。
- 提交分批、单一职责；默认分支是 `gpu-path1`。
- 本单之外同期已完成并推送的改动见 `git log 62b159b..HEAD`（23 项修复 + 字体 +
  文档订正），别重复做。
