# JIT 诊断交接 — 2026-10-07 23:46

用户因 Daybreak 拦截要求先交接、清除上下文；本轮停止推进构建/部署。不要重做已完成分析。用户已明确授权真机诊断部署，设备在线，并确认亮屏/Wi-Fi 正常；后续任务继续这些已授权工作，工具本身的沙箱提权流程仍需遵守。

## 一句话结论与当前断点

已定位 ARM32 Baseline `put_by_val` 的 **ArrayProfile 参数与 DataIC handler 同占 r9**。分发覆盖 profile 指针，慢路径把数组类型位图写进 InlineCacheHandler 的函数指针槽，下一次 `blx r6` 跳到 `0x40000` / `0x100`。

**最小修复已写入并且 JSC 重建成功（退出码 0）。还没重编最新驱动、没打包/部署 0.2.5.14、没验证修复效果。不要声称“真机已修好”。**

先读 `docs/JIT-ARM32-R9-ROOT-CAUSE-2026-10-07.md`。原始 `docs/CONSULT-2026-10-07.md` 是早期猜测，不要继续优先查“thunk 表没填满”。

## 已完成的有效真机证据

精确版本 0.2.5.13，均冷启动，测试页 `https://cn.bing.com/`，配置在引擎初始化前固定：

- `crash/jit-analysis-20261007/mode1-20261007-224713/`：useJIT=1、baseline=0、pid=4848；Bing 加载 rc=0、14 scripts；120 秒无新增崩溃。
- `crash/jit-analysis-20261007/mode2-20261007-224945/`：useJIT=1、baseline=1、pid=4068；启动约 5 秒复现 PC=0x40000、LR=0x1bc11b15，池起点 0x1bc10000。与历史 0.2.5.9/.10 的池内 LR 偏移 +0x1b15 相同。
- 0.2.5.13 EXE/PDB/sha256 在 `crash/jit-analysis-20261007/0.2.5.13/`，另有 .9、.10、.12 的精确归档。

第一条新执行违例：2026-10-07 22:49:50.299，r0/r1=base payload/CellTag，r2/r3=property/int32，r4/r5=value/int32。r8=0x296bbaa8（PropertyInlineCache），r9=0x299cc560（handler），r6=0x40000。

反汇编 `mode2-20261007-224945/lr-code.disasm.txt`：

```asm
1bc11afc subw  r8, r11, #0x268
1bc11b00 addw  r9, r10, #0x1a8   ; 正确 ArrayProfile*
1bc11b04 adds  r6, r1, #5
1bc11b06 bne.w slow_case
1bc11b0a ldr.w r9, [r8, #0x24]  ; 覆盖为 handler*
1bc11b0e ldr.w r6, [r9, #0x0c]  ; m_callTarget 已被写坏
1bc11b12 blx   r6               ; 跳 0x40000
```

证据边界：捕获了覆盖 profile 的指令和读错跳靶的现场，首次动态写坏 callTarget 未被单步捕获；写坏机制由下面源码链支撑，修复前后对照尚待完成。历史两个 0x100 崩溃无 LR 机器码，不能说已独立逐指令验证。

## 根因源码链（避免重复搜索）

1. `WebKit/Source/JavaScriptCore/jit/GPRInfo.h` ARM32 部分：handlerGPR=r9；preferredArgument 的寄存器序列 r0,r1,r2,r3,r4,r5,r8,r9,r10。
2. `jit/BaselineJITRegisters.h` PutByVal：三个 JSValue 占 r0-r5，propertyCache=r8，原 profileGPR=r9。
3. `jit/JITPropertyAccess.cpp` emit_op_put_by_val 构造参数；`jit/JITInlineCacheGenerator.cpp:58` emitDataICHandlerDispatch 无条件 load handler 到 r9。
4. `bytecode/InlineCacheCompiler.cpp:1638` putByValSlowPathCodeGenerator 把 profileGPR 当 ArrayProfile* 传给 C++ 慢操作。
5. `jit/JITOperations.cpp` putByValOptimize 调用 profile->computeUpdatedPrediction。
6. `bytecode/ArrayProfile.cpp` computeUpdatedPrediction 首次修剪会直接令 observedModes=当前类型位。ArrayProfile::m_observedArrayModes 偏移 +12，InlineCacheHandler::m_callTarget 也 +12。
7. 0x40000 = Int32ArrayMode = 1<<18；0x100 = 1<<NonArrayWithContiguous（ContiguousShape=8）。因此低地址是统计位图覆盖函数指针，并非随机地址，也不是 JIT 页 DEP 的直接故障。

JITThunks 某些32位不支持的 common thunk 允许返回空，不能加“全表必须非空”断言。旧日志 cand 是粗略扫描、不是完整栈，不能用于杜撰逐层回溯。

## 本轮源码/脚本改动

- `WebKit/Source/JavaScriptCore/jit/BaselineJITRegisters.h`：仅 `defined(WK_WINUWP) && CPU(ARM_THUMB2)` 下 PutByVal::profileGPR=r10（metadataTableRegister）；添加 live 参数与 handlerGPR 不重叠的 static_assert。原头备份 `crash/jit-analysis-20261007/BaselineJITRegisters.before.h`；独立补丁 `putbyval-r9-fix.patch`。
  - r10 可借用：emit_op_put_by_val 调用后本来就恢复 metadata/jitData；putByValSlowPathCodeGenerator 的 C++ 调用后也已有同样 ARM_THUMB2 恢复逻辑。CCallHelpers 先把参数寄存器写入栈槽，再执行参数搬移。
  - 32位 scratch1 可以和 handler 的 r9 复用（调用前统计临时寄存器），不要误加 scratch 与 handler 不重叠断言。
- `port/WebCoreDriver.cpp`：LocalState/jitdiag.txt 冷启动模式 0=原 shipping 策略，1=useJIT开/Baseline关，2=都开；在 JSC 初始化前设配置且禁止 UI 设置覆盖。记录实际 mode/baseline/pid。VEH 保存完整寄存器、LR前256后128字节、异常SP栈、关键指针内存。最新再补 r4/r8/r9 内存快照，这三项**还没编进驱动**。原文件备份 `crash/jit-analysis-20261007/WebCoreDriver.before.cpp`。
- `harness/MainPage.xaml.cpp`：WebCoreSetCrashLogPath 移到 WebCoreSetProfilePath 前面！后者内部会 ensureWebCoreInitialized，否则配置来不及生效。
- `harness/Package.appxmanifest`：当前源码版本 **0.2.5.14**。最新存在的 appx 仍是 **0.2.5.13**。
- `tools/jit-diagnostic-run.ps1`：WDP冷启动、multipart写模式、可选TestUrl、每10秒采样、保存pre/post日志；最后验证实际 mode/baseline。**默认 Ver 仍为0.2.5.13，验证14必须显式 -Ver 0.2.5.14。**
- `tools/disassemble-jit-log.ps1`：选最后一个带 access=8 与 lr-code 的事件，用 clang 组装原始bytes、llvm-objdump解码；实际地址为输出第一列，分支目标注释仍为片段相对地址。别挑最后一个任意SEH：主故障后还记录了其他异常。
- `docs/JIT-ARM32-R9-ROOT-CAUSE-2026-10-07.md` 与本交接是新增文件。工作区本来有很多用户改动，不要整库回退或全部提交。本轮没有提交。

无 C ABI 导出变更，不需要改两份 WebCoreDriver.h。

## 构建状态（已核实）

JSC 构建命令：

```powershell
. E:\Apotheosis\port\arm32-uwp-env.ps1
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -C E:\Apotheosis\build-clang-gpu -j4 JavaScriptCore
```

- 本次因旧 deps mtime 与 obj 不一致、Ninja 报 premature end of file，实际重编 549 项（bmalloc/WTF/JSC），不是只有改动TU。已完成，**退出码0**。
- 日志 `crash/jit-analysis-20261007/jsc-fix-build.log` 末行为 `[549/549] Linking ... JavaScriptCore.lib`。
- JavaScriptCore.lib 时间 **2026-10-07 23:31:06**。
- 最后核查 **23:45:40**：没有 ninja/clang/MSBuild 在运行。原 tool session17052已收尾，退出码0，无需再等待。
- WebCoreDriver.cpp 最新23:25；WebCoreDriver.gpu.obj仍22:24、WebCoreDriver-gpu.lib仍22:27，**必须重编驱动**。
- 本机 build-harness.ps1 实际默认 XamlMode=Fallback（官方XAML因本地工具链故障回退）。本轮12/13都按这个既有默认构建通过，不要仅凭旧HARNESS-BUILD文档强改为Official。

## 下一步（顺序执行）

1. `pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1`，确认EXIT=0、库更新时间。
2. `pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1`，确认产生 Harness_0.2.5.14_ARM.appx。归档对应 EXE/PDB/hash 到 analysis/0.2.5.14，不能覆盖13证据。
3. 非破坏部署14；设备 `https://192.168.3.159:443`，无账户密码、需要CSRF cookie/header，禁用代理。不要用卸载数据的deploy-launch。
4. **Deploy-Robust有已知假成功**：上传202后首次安装state可能204，它就误以为结束并查到旧版、launch500仍打印成功。必须再查 /api/app/packagemanager/state 与 packages，确认真实包名含0.2.5.14后再跑测试。不要据脚本最后一行下结论。
5. 修复后先跑原来的失败配置：

```powershell
pwsh -NoProfile -File E:\Apotheosis\tools\jit-diagnostic-run.ps1 -Ver 0.2.5.14 -Mode 2 -WaitSec 180 -TestUrl https://cn.bing.com/
```

核对 stage 中 useJIT=1 baseline=1，页面确实 after-load rc=0；检查崩溃签名是否变化。若原低地址违例消失但出现 ScheduledTask SIGABRT，应区分两个故障，不能说JIT仍是相同问题，也不能宣称所有崩溃全修好。
6. 若通过，做合理的重复冷启动/轻量数组写入覆盖；可用 testurl.txt 的 data:text/html 测试页构造 Int32Array 普通数组循环，**目前尚未创建或执行这种最小JS用例**。
7. 完成后将 jitdiag 置0，并删除本轮新增 testurl.txt（原文件不存在，见有效mode1目录pre文件清单），恢复用户首页设置。Mode0脚本本身不会删除testurl，需通过带CSRF的WDP DELETE file清理后冷启。用户settings.ini没有被我们改写；最初 jit=0、home=空。
8. 更新根因文档真实验证结果，并把最小上游补丁持久化到项目的补丁管理机制；当前仅新增了独立patch，**未重生成整个wk-winuwp.patch**。避免覆盖其他在途改动。

## 设备/实验陷阱

- 手机目前装13，LocalState/jitdiag.txt=2，testurl.txt=https://cn.bing.com/。所以直接再开13会继续触发未修复故障。此次交接没有再次启动/修改手机。
- 设备曾因息屏导致WDP超时，用户确认亮屏后恢复。可提醒保持亮屏，不要重复要求授权部署。
- WDP multipart 必须手工带引号的 Name='"file"'、FileName='"jitdiag.txt"'。Multipart.Add(data,'file','name')生成的未加引号头曾400。新脚本已修正。
- 0.2.5.12 的两个完成样本都实际 useJIT=0，原因是profile先初始化JSC，**无效**。不要把它们120秒没崩用于任何JIT稳定性结论。
- 构建始终最多 `ninja -j4`；机器16GB，不要12并行。遵守ASCII路径、clang引擎/MSVC harness工具链分工和WK_WINUWP守卫。
- 用户说Daybreak一直拦截，所以要求本次先交接；没有收到可供引用的具体自动审查拒绝原因，不要编造。

## GitHub 归档补充

用户随后要求推送本轮更改。本轮源码增量保存为 patches/ 下的三个独立补丁（引擎修复、诊断驱动、宿主初始化顺序及版本号），连同工具与本交接提交；未将之前未提交的UI/驱动基线整批纳入。诊断补丁依赖本机0.2.5.11工作基线，不是对远端HEAD的完整移植升级；详见 patches/README-JIT-2026-10-07.md。本机三个补丁均已应用，续接时不要重复apply。
