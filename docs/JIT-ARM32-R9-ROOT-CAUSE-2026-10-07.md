# ARM32 JIT 低地址执行违例：PutByVal 与 DataIC 的 r9 冲突

2026-10-07。状态：已取得原始故障指令并形成可解释历史签名的根因链；最小修复已写入，正在重建，真机修复验证尚未完成。

## 真机对照

相同 0.2.5.13 EXE/PDB、相同冷启动入口 https://cn.bing.com/：

| 配置 | 结果 | 证据目录 |
|---|---|---|
| useJIT=1, useBaselineJIT=0 | 加载 rc=0，14 scripts，120 秒未新增崩溃 | crash/jit-analysis-20261007/mode1-20261007-224713 |
| useJIT=1, useBaselineJIT=1 | 启动约 5 秒后 execute AV，PC=0x40000、LR=池+0x1b15 | crash/jit-analysis-20261007/mode2-20261007-224945 |

两组配置均由 stage.txt 的 diagnosticMode/baseline/pid 确认。此前 0.2.5.12 的两组样本因为初始化顺序错误，实际 useJIT=0，已排除，不计作证据。120 秒只覆盖本次页面路径，不证明所有非 Baseline 路径都正确。

## 故障指令与寄存器

2026-10-07 22:49:50.299，池 [0x1bc10000,0x1dc10000)：

```asm
1bc11afc  subw  r8, r11, #0x268   ; HandlerPropertyInlineCache*
1bc11b00  addw  r9, r10, #0x1a8   ; ArrayProfile*（此刻正确）
1bc11b04  adds  r6, r1, #5        ; CellTag 检查
1bc11b06  bne.w slow_case
1bc11b0a  ldr.w r9, [r8, #0x24]  ; IC.m_handler，覆盖 ArrayProfile*
1bc11b0e  ldr.w r6, [r9, #0x0c]  ; handler.m_callTarget
1bc11b12  blx   r6               ; r6=0x00040000，执行违例
```

异常现场：r0=0x29da2128，r1=0xfffffffb（CellTag），r2=0x19cf，r3=0xffffffff（Int32Tag），r4=0x39d9，r5=0xffffffff；这与 put_by_val 的 base/property/value 三个 JSValue 参数对一致。r8=0x296bbaa8（IC），r9=0x299cc560（handler），r10=0x29055ad8（metadata），r11=0x296bbd10（JITData）。

raw crash.txt 中同一次失败后还有其他异常记录，解码必须挑 access=8 的事件；lr-code.disasm.txt 首列是实际地址，第二列为截取片段偏移，分支标注的目标仍为片段相对值。

## 源码链

1. jit/GPRInfo.h：ARM32 regT7=r9；handlerGPR=nonPreservedNonArgumentGPR2=r9。preferredArgumentGPR 的 ARM32 参数寄存器序列是 r0,r1,r2,r3,r4,r5,r8,r9,r10。
2. jit/BaselineJITRegisters.h 的 PutByVal：base=r0/r1，property=r2/r3，value=r4/r5，PropertyInlineCache*=r8，ArrayProfile*=r9。原来的 32 位分支未断言 profile 与 handler 不重叠。
3. jit/JITPropertyAccess.cpp 的 emit_op_put_by_val 构造上述参数；jit/JITInlineCacheGenerator.cpp 的 emitDataICHandlerDispatch 把 handler 装进 r9，再 call [r9+offsetOfCallTarget]。
4. bytecode/InlineCacheCompiler.cpp 的 putByValSlowPathCodeGenerator 仍将 profileGPR 传给 operationPutByValStrictOptimize/SloppyOptimize；它看到的实际是 InlineCacheHandler*，不是 ArrayProfile*。
5. jit/JITOperations.cpp 的 putByValOptimize 调用 profile->computeUpdatedPrediction(codeBlock, structure)。
6. bytecode/ArrayProfile.cpp 的 computeUpdatedPrediction 先将类型位 OR 入 m_observedArrayModes，首次修剪时可直接赋值为当前类型位。ArrayProfile::m_observedArrayModes 在 +12；ARM32 InlineCacheHandler::m_callTarget 也在 +12。因此这次统计写入覆盖下一次间接调用的代码指针。
7. 下一次 DataIC 分发取回该位图作为函数指针，blx r6 跳到不可执行的低地址。

这同时解释规整低地址的来源：

- 0x40000 = 1 << 18 = Int32ArrayMode（ArrayProfile.h）。本次 base 的 JSType 字节为 0x36；仍需避免将单字节脱离完整类型定义单独当成额外证据。
- 0x100 = 1 << 8 = asArrayModesIgnoringTypedArrays(NonArrayWithContiguous)，其中 ContiguousShape=0x08（IndexingType.h）。历史两个 0x100 崩溃未保存原始 JIT 指令，因此它们与该机制高度一致，但没有独立逐指令验证。

现有样本直接证明了被覆盖的 r9 和从 handler+12 取得错误跳靶；第一次破坏该字段的动态写入未被单步捕获。以上写坏过程由参数分配、实际指令和对应 C++ 写入逻辑共同支撑，修复对照用于进一步验证。

## 最小修复

仅在 defined(WK_WINUWP) && CPU(ARM_THUMB2) 下，将 PutByVal::profileGPR 改为 GPRInfo::metadataTableRegister（r10），并对全部 live 参数及 handlerGPR 添加 noOverlap 静态断言。保留 r9 给 handler 分发。

复用 r10 的条件已核对：emit_op_put_by_val 调用返回后，以及 putByValSlowPathCodeGenerator 的 C++ 慢调用返回后，都已有 ARM_THUMB2 专用 emitMaterializeMetadataAndConstantPoolRegisters 恢复代码；参数入栈先写完各寄存器，再做参数搬移。未改变 C ABI、JSValue 编码、代码页权限或全局 handler 寄存器选择。

引擎构建严格 ninja -j4 JavaScriptCore。构建缓存的 deps mtime 与对象时间不一致且报告 premature end of file，Ninja 决定重编 549 项（含依赖），没有绕过依赖检查。

0.2.5.14 将打包修复及额外 r8/r9 内存采集。默认 shipping JIT 策略保持关闭，诊断通过 LocalState/jitdiag.txt 显式选择，完成后恢复模式 0 并清理本轮 testurl.txt。

## 23:46 构建状态更新

JSC 549 项重建已成功，工具返回退出码0，JavaScriptCore.lib 更新时间23:31:06。没有后台编译进程。驱动最新额外r4/r8/r9采集尚未重编；0.2.5.14尚未打包/部署，修复真机验证待完成。用户因Daybreak拦截要求交接并清上下文；续接请读 docs/HANDOFF-JIT-2026-10-07.md。
