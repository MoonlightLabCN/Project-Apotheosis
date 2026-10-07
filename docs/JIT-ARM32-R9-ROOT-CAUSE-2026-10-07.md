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

## 00:30 更新：r9 修复已验证通过；另发现第三个崩溃（与 JIT 无关）

### r9 修复真机验证通过（0.2.5.14，mode2 复跑原失败配置）

`tools/jit-diagnostic-run.ps1 -Ver 0.2.5.14 -Mode 2 -WaitSec 180 -TestUrl https://cn.bing.com/`
- stage.txt 确认 `useJIT=1 canUseJIT=1 diagnosticMode=2 baseline=1 pid=172`
- **180 秒零新崩溃**；cn.bing.com `rc=0`（17 脚本），pigai.shop `rc=0`
- 对照：同配置在 0.2.5.13 上约 5 秒即 PC=0x40000 / LR=池+0x1b15
- 证据目录 `crash/jit-analysis-20261007/mode2-20261008-000555`，EXE/PDB 归档在同级 `0.2.5.14/`

### 新崩溃：`HTMLSelectElement::showPopup` 在 `selectedIndex()` 无效时空解引用 RefPtr

**触发操作**（用户提供）：apple.com/iphone-18-pro/ → 点"选机型对比"下拉框 → 选"和 iPhone 14 对比"。
自动加载该 URL 即可复现（页面自身 JS 触发），不必手动点。

minidump（`mode2-20261008-002236/Harness.exe.1932.dmp`，cdb 完整栈）：

```
WebEngine::loop → WebCoreClickAt → EventHandler::handleMousePressEvent
→ EventHandler::dispatchMouseEvent → Element::dispatchMouseEvent
→ EventDispatcher::dispatchEvent → HTMLSelectElement::defaultEventHandler
→ HTMLSelectElement::menuListDefaultEventHandler
→ HTMLSelectElement::openPickerForUserInteraction
→ HTMLSelectElement::showPickerInternal → showPopup
```

异常：`SEH code=0xc0000005 address=0x009b60c8 access=0 fault=0x00000000`，
`pc=0x009b60c8 ldr r0,[r10]`，**`r10=0`**。`jit-context: mode=2 useJIT=1 baseline=1`
（但根因与 JIT 无关，见下）。

现场反汇编（`0x9b6060–0x9b60c8`，即 `showPopup+0x200` 到 `+0x24c`）：

```asm
009b6064  bl     HTMLSelectElement::selectedIndex   ; r6 = selectedIndex()
009b606a  ldrb   r0,[r7,#0xED]                      ; usesBaseAppearancePicker?
009b6078  bl     recalcListItems
009b607c  mov    r3,#0xFFFFFFFF                     ; r3 = -1（listIndex 无效值）
009b6080  cmp    r6,#0
009b6082  bmi    → showPopup+0x24c (009b60c8)       ; selectedIndex() < 0
009b6084  ldr    r0,[r7,#0x90]                      ; listItems().size()
009b6088  cmp    r6,r0
009b608a  bge    → showPopup+0x24c (009b60c8)       ; selectedIndex() >= size
009b608c  ...（正常路径：下标合法，走 ldr r5,[r2,r3,lsl #2]）
...
009b60c8  ldr    r0,[r10]      ; ← r10==0，空解引用
009b60d0  ldr    r7,[r0,#4]
009b60d4  blx    r7            ; 虚调用（Ref 的 deref）
009b60d6  ldr    r0,[r10,#4]
009b60da  subs   r0,#1         ; refCount-1
```

**解释**：`selectedIndex()` 越界时编译器生成了 `+0x24c` 这条提前退出分支，
对应源码 `HTMLSelectElement.cpp:2234`
`m_popup->show(absBounds, *frameView, optionToListIndex(selectedIndex()))`。
`optionToListIndex()`（1427-1444 行）在下标非法时返回 **-1**，随后
`EmptyPopupMenu::show()`（`loader/EmptyClients.cpp:536`）是**空实现**，什么都不做。
问题在于这条"无效下标"路径上一个栈上 `RefPtr` 的指针为 0，而清理代码
（`ldr r0,[r10]` + `blx` 虚调用 + 引用计数自减）**没有判空**——同函数的正常分支
对 `r7` 的偏移都做了保护。即：**要么是 clang 对 ARM32 Thumb-2 在某条 phi/清理边
上的代码生成漏了判空，要么上游期望该路径下对象必非空而 port 场景打破了该假设**。

**与 JIT 无关**：同样页面在 mode 0/1 未复测，但崩点是 WebCore 的 select 弹窗 +
本 port 的 `EmptyPopupMenu`，与 baseline JIT 无关（bing/pigai 无可用 select 弹窗路径，
所以此前从未暴露；"时崩时不崩"是路径未覆盖，不是竞态）。

**注意**：早前我从错误字节边界反汇编得到的 `vcmp.f32` 等是乱码解析，已作废；
`pc in Harness.exe +0x008360c9` 的符号化结果（`stringTypeAdapterAccumulator`）是
就近符号，不代表真实函数，亦作废。

**下一步**：给 `showPopup()` 的无效下标分支加 `m_popup` / `r10` 判空（或直接
`return` 前确保 `m_popup` 有效），并在 mode 0 与 mode 2 各复跑该 URL 做对照。

## 01:20 更新：showPopup 判空修复已完成并真机验证（0.2.5.15）

**修复**（`WebKit/Source/WebCore/html/HTMLSelectElement.cpp`，WK_WINUWP 守卫）：
在 `protect(m_popup)->show(...)` 之前补两道判空——
```cpp
if (!m_popup) return;                              // createPopupMenu() 可能返回 null
int listIndex = optionToListIndex(selectedIndex());
if (listIndex < 0) return;                         // 无效下标路径不做无效调用
protect(m_popup)->show(absBounds, *frameView, listIndex);
```
根因（比第一次分析更准）：`r10` 就是 `m_popup`。`protect(m_popup)` 在 null `RefPtr` 上构造
`Ref`，其析构无条件调 `ptr->deref()` → `nullptr->deref()` → `ldr r0,[r10]`（r10=0）+
虚调用 + 引用计数自减。`PortChromeClient::createPopupMenu()` 返回 nullptr
（`port/PortChromeClient.cpp:32-35`），上游 `EmptyChromeClient` 则返回 no-op 的
`EmptyPopupMenu`（`loader/EmptyClients.cpp:624-627`）——port 这个 nullptr 与上游假设不符。

**真机验证**（0.2.5.15，mode 2，`jit-diagnostic-run.ps1 -Ver 0.2.5.15 -Mode 2
-WaitSec 180 -TestUrl https://www.apple.com/iphone-18-pro/`）：
- stage.txt 确认 `useJIT=1 canUseJIT=1 diagnosticMode=2 baseline=1`
- **180 秒零新崩溃**；`after-load .../iphone-18-pro/ rc=0 compositing=1`，
  正常渲染 28300px 高的完整页面
- 对照：同页面在 0.2.5.14 上约 40 秒即 `pc=showPopup+0x24c`
- 证据目录 `crash/jit-analysis-20261007/mode2-20261008-011610`；EXE/PDB 归档在
  `0.2.5.15/`（EXE sha256 F84254DFD40BE1F5DFE5E0474DEC4933FABE31B5E73980E2561DD4290B1B4659）

**注意**：`optionToListIndex()` 仍是 O(n) 线性扫描（`HTMLSelectElement.cpp:1427-1444`），
只在这里调一次；若将来要支持很大的 `<select>`，应缓存 option 索引表。

**仍未修**：`<select>` 本身仍然弹不出东西（`createPopupMenu` 返回 nullptr 只是不崩了）。
要真能用得在 port 侧做弹窗 UI（`PortUIBridge.h:65` 已预留 `UIRequestSelect=5`，
FileChooser 的 request/response + staleness 模式可照抄）。见
`docs/FEATURE-GAPS-2026-10-08.md` §1。


