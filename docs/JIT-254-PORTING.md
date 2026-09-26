# WebKit 2.54 的 JSC JIT ARM32 适配现状（2026-09-26）

## 结论（当前构建取 LLInt）

2.54 重基线后，`-DAPOTHEOSIS_JIT=ON`（JIT 启用）的构建**尚未适配完成**，当前交付走
LLInt（解释器）配置。这与历史构建的实际状态一致：build-clang-gpu / build-clang-jit /
build-clang-webcore 三条线的 JavaScriptCore.lib 经符号级验证**均无** JIT 符号
（`startOfFixedExecutableMemoryPoolImpl` 计数 0）——历史构建的 JIT 一直是静默关闭的
（wtf/PlatformEnable.h 对非 Linux 32 位一律 `#undef ENABLE_JIT`，除非定义
`WK_WINUWP_JIT`；而旧 build.ninja 中该定义为 0 处）。

门控现已接好（启用即真开 JIT）：

- `Source/cmake/OptionsWinUWP.cmake`：`if (APOTHEOSIS_JIT) add_definitions(-DWK_WINUWP_JIT=1)`
- `port/configure-gpu.ps1`：`-DAPOTHEOSIS_JIT`（当前 OFF，改 ON 即开）
- `port/compile-driver-gpu.ps1`：驱动侧同步 `-DWK_WINUWP_JIT=1`
- `port/WebCoreDriver.cpp`：crash.txt 的 jit-pool 行已 `#if ENABLE(JIT)` 守卫，
  LLInt 构建打印 `(LLInt build, no JIT pool)`。

## 已落地的 JIT 编译期修复（启用后仍保留，无副作用）

1. `Source/JavaScriptCore/assembler/MacroAssemblerARMv7.h`：单寄存器
   `branchTest64(cond, reg)` 占位重载（2.54 的 `AssemblyHelpers::emitAllocateJSBigInt64`
   模板体非依赖调用需要；该模板在 ARM32 永不实例化）。
2. `Source/JavaScriptCore/runtime/JSCJSValue.h`：显式 `#include <wtf/Atomics.h>`
   （`updateEncodedJSValueConcurrent` 用 `WTF::storeStoreFence`，LLIntOffsetsExtractor
   的 include 顺序下 Atomics.h 未展开）。
3. `Source/JavaScriptCore/bytecode/InlineCacheCompiler.h`：显式
   `#include <JavaScriptCore/CCallHelpers.h>`（`CCallHelpers::Jump` 嵌套类型需完整类型）。
4. `Source/JavaScriptCore/bytecode/CodeBlockInlines.h`：显式 `#include "JumpTable.h"`
   （`m_switchJumpTables[i]` 容器元素需完整类型，FixedVector/span 内部指针算术）。
5. `Source/JavaScriptCore/bytecode/PropertyInlineCache.h`：JSVALUE32_64 块补
   `propertyPayloadGPR()` 访问器（2.54 的 InlineCacheCompiler.cpp 在 32 位分支引用了
   这个从未声明的成员——上游 32 位路径年久失修）。
6. `Source/JavaScriptCore/bytecode/InlineCacheCompiler.cpp`（ArrayLengthStore）：
   64 位单寄存器洞值填充改为 JSVALUE32_64 双字语义（高半 scratchGPR、低半借
   `valueRegs.tagGPR()`，循环内两个 store32；`branchIfNotInt32` 检查上移使 tag 寄存器
   在复用前已用尽）。
7. `Source/JavaScriptCore/bytecode/InlineCacheCompiler.h`/`.cpp` 的
   JITInlineCacheGenerator 族：还差 `DFG::UnlinkedPropertyInlineCache` 的显式包含
   （下一波会撞，修法同 3/4）。

## 启用 JIT 前仍需完成（按撞墙顺序）

1. **wtf/Variant.h 的 std::variant 替换 vs 2.54 的 switchOn**：
   `std::visit(WTF::Visitor<...>)` 在部分 2.54 JIT TU 实例化时报
   "no matching function for call to 'invoke'"（MSVC STL variant 1564）。
   mpark::visit 与 std::visit 的可调用判定有差异，`WTF::Visitor` 重载集需要一个
   适配层或泛型兜底。这是 WTF 核心模板，改动要谨慎。
2. JITInlineCacheGenerator.h 的 UnlinkedPropertyInlineCache 完整类型（浅）。
3. 后续批次未知（基线 JIT 的 64 位惯用法在 2.54 里新增了不少；DFG/FTL 本 port 关闭）。

## 复现/继续的方法

```powershell
# 打开 JIT：
#   port\configure-gpu.ps1 里 -DAPOTHEOSIS_JIT 改 ON
pwsh -File port\configure-gpu.ps1
pwsh -File port\build-engine-gpu.ps1   # ninja 断点续跑,可反复调用
```

每批错误的共同形态：2.54 的 JIT 代码按 64 位单寄存器假设书写（上游已不构建 32 位
JIT），要么补 include/访问器（浅），要么改双寄存器对语义（中），个别涉及模板
决议（深）。历史上该 port 在 2.52 的 JIT（build-clang-jit 线）另行验证；
2.54 的差异面集中在 DataIC/BigInt64/IC 生成器三块。
