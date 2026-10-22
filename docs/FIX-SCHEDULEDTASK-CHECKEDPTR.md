# 修复记录：`RedBlackTree::remove()` 未清 CheckedPtr → `ScheduledTask` 崩溃

**日期**：2026-10-22
**分支**：`fix/scheduled-task-checkedptr`
**版本**：0.8.0.5（基线 0.8.0.0）
**真机现场**：`crash.txt` 2026-10-22 22:59:05（Lumia 950 / 15254.603）

---

## 1. 症状

单次事件在**同一秒内**产生 7 条记录（同一根因的多阶段）：

```
22:59:05.107  reason: WTF checked-ptr: ScheduledTask deleted with 1 CheckedPtr(s) outstanding (zeroed and leaked)
22:59:05.215  reason: WTF checked-ptr: released a CheckedPtr to an already-deleted object
22:59:05.216  reason: signal 22 (SIGABRT) ws=386M commit=287M availPhys=1356M availVirt=891M load=47%
22:59:05.307  reason: pc in CoreUIComponents.dll +0x000d4347  lr in CoreUIComponents.dll +0x000d77c5
22:59:05.318  reason: SEH code=0xc0000005 address=0x73cf4347 access=1 fault=0x00000000
22:59:05.319  reason: fault page: base=0x00000000 ...
22:59:05.319  reason: jit pool: [0x1bd10000,0x1dd10000) isJITPC(fault)=0
```

**末三条是崩溃处理器自身的栈**（`CoreUIComponents.dll`），不是根因——容易被误判。
真正的原因在第一条，随后 `SIGABRT`。

> ⚠️ 注意：`jit-context: useJIT=1` / `isJITPC(fault)=0` 是**环境印记**，本崩溃与 JIT **无因果关系**
> （`pc` 明确不在 JIT 池 `[0x1bd10000,0x1dd10000)` 内）。这一点已连续两次误导定位。

## 2. 根因

`WTF::RedBlackTree::NodeBase` 的子链接**是 `CheckedPtr`**（`RedBlackTree.h:157-158`）：

```cpp
CheckedPtr<NodeType> m_left;
CheckedPtr<NodeType> m_right;
```

而 `remove()`（`RedBlackTree.h:228`）把节点从树里摘除时，**只改了别人的链接，从不清除被摘节点自己的链接**：

```cpp
NodeType* remove(NodeType* z)
{
    ...
    if (y != z) {
        ...
        y->setLeft(z->left());     // ← y 指向 z 的孩子
        y->setRight(z->right());   // ← 但 z->m_left/m_right 仍然持有这两个 CheckedPtr!
        ...
    } else if (y->color() == Black)
        removeFixup(x, xParent);

    return z;                      // ← z 带着陈旧的 CheckedPtr 返回
}
```

**上游靠什么兜住？** `insert()` 开头的 `x->reset()`（第 176 行）：

```cpp
void insert(NodeType* x)
{
    x->reset();        // 清掉上次遗留的 m_left/m_right
    treeInsert(x);
    ...
}
```

**对"移除后还会重新插入"的节点，这个兜底有效**。但 `RunLoop::TimerBase::ScheduledTask` 的生命周期是
**移除 → 销毁**（`~TimerBase()` 里 `stopWithLock()` 解链，随后 `m_scheduledTask` 释放），**不会重新插入**
→ 陈旧的 `CheckedPtr` 一直留到对象被删除 → `WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR` 报
`1 CheckedPtr(s) outstanding` → 上游的检查点把它当致命错误 → `SIGABRT`。

### 2.1 与上一轮修复的关系

`RunLoopGeneric.cpp:328` 已有上一轮加的 `detachScheduledTaskWithLock()`（安全网，从 `~TimerBase()` 调用），
它确实在**解链**并报警。但它**救不了链接本身**——因为问题不在"节点还在树里"，而在
**"节点已不在树里，但它自己还持有指向旧孩子的 CheckedPtr"**。

两个修复是互补的：
- 上一轮：保证节点**确实**被解链（防御标志位与树不一致）
- 本轮：保证解链后**节点自身不再持有任何 CheckedPtr**

## 3. 修法

`remove()` 返回前清除被摘节点自己的链接（`RedBlackTree.h`，`#if defined(WK_WINUWP)` 守卫）：

```cpp
        } else if (y->color() == Black)
            removeFixup(x, xParent);

#if defined(WK_WINUWP)
        // Apotheosis (2026-10-22): clear the unlinked node's own child links.
        // ... (real-device evidence in the comment)
        z->reset();
#endif
        return z;
    }
```

**正确性论证**：
- `y != z` 分支：`y->setLeft(z->left())` 与 `z->m_left` 此时指向**同一对象**（CheckedPtr 计数为 2）；
  `z->reset()` 释放其一，计数回到 1（`y` 持有）→ **正确**。
- `y == z` 分支：`z` 的孩子链接本就不再被树引用，清空即释放 → **正确**。
- `reset()` 还会把 `m_parentAndRed` 置回 `1`（红 + 空父）。插入点在所有 `z->left()/right()/parent()`
  的读取**之后**，后续无代码再读 `z` 的这些字段 → **安全**。
- `reset()` 是 `NodeBase` 的 `private` 成员，但 `NodeBase` 有 `friend class RedBlackTree;`（第 59 行）
  → **可访问**。

## 4. 顺带修复：补丁漏项（重要）

生成补丁时发现，**上一轮会话的 CheckedPtr 修复从未纳入补丁清单**：

| 文件 | 改动 |
|---|---|
| `Source/WTF/wtf/CheckedRef.h` | +34 行（`WTFWinUWPReportDanglingCheckedPtr` 等） |
| `Source/WTF/wtf/Assertions.cpp` | 69 行（诊断钩子） |
| `Source/WTF/wtf/FastMalloc.h` | `WTF_WINUWP_REPORT_CHECKED_PTR_DELETION` 宏 |
| `Source/WTF/wtf/generic/RunLoopGeneric.cpp` | +40 行（`detachScheduledTaskWithLock`） |

`WebKit/` **不在仓库**——未被补丁覆盖的改动**随时会丢**。

补丁已从 **7 文件扩到 12 文件**（162 KB，反向自检 `git apply --check -R` 通过）。
**这是本项目第二次因"补丁清单不全"而面临丢失风险**（第一次是媒体 4 个新文件）。

## 5. 验证

- ✅ `ninja WTF` rc=0（`RedBlackTree.h` 是所有 WTF/JSC/WebCore TU 的公共头，重编量大）
- ⏳ 全链构建 0.8.0.5 → 真机部署 → 观察 `crash.txt` 是否还出现 `ScheduledTask ... outstanding`
- **回归判据**：页面加载 / 定时器密集页面（大量 `setTimeout`/动画）不应新增崩溃

## 6. 教训

1. **`CheckedPtr` 是"外部引用计数"，对象自己的成员也可能是引用方。** 移除一个节点不等于
   该节点不再引用任何人——`remove()` 的契约应考虑**双向**解绑。
2. **上游的"下次 insert 时清理"是一种延迟兜底，只对"会重插"的对象成立。** 依赖它的代码在
   "移除即销毁"路径上会漏。
3. **末几条 `reason:` 往往是崩溃处理器自身的栈**（`CoreUIComponents.dll` / `SEH 0xc0000005`），
   定位要看**第一条**和 `signal 22`。
4. **`jit-context: useJIT=1` 不等于"JIT 崩了"** —— 本项目已两次因此误判。
5. **补丁清单必须逐文件核对**，`git status` 里的 `M`/`MM` 都可能是未入库的工作。
