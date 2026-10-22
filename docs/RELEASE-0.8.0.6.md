# Release v0.8.0.6

**日期**：2026-10-23　**分支**：`media-mf`　**提交**：`30d1b1c`

## 本版内容

### 修复

- **`<video>` 终于有声音了（真机确认）** —— 根因是 `AudioGraph` 里**从未创建
  `AudioDeviceOutputNode`、也从未 `AddOutgoingConnection`**。悬空的输入节点没有消费者，
  音频图便以最大速度拉帧丢弃（正是本项目连续多轮读到的 ~17 倍速空转）。接上后量子节奏
  从 ~1500/秒 回到 ~100/秒（真实设备时钟），用户真机确认"听到声音了"。
- **播放结束闪退** —— 我上一版加的"空转自停"在 `QuantumStarted` 回调**内部**拆音频图，
  在 `Windows.Media.Audio.dll` 里 fault 并把 app 带走。改为 `CoreDispatcher` 派发到 UI
  线程执行，并修掉"设旗后不 return 导致日志刷 25621 行"与"停止从未执行"两个 bug。
- **UI 线程空指针崩溃**（`ScheduledTask` 无关的那条）—— `AudioLog` 直接解引用
  `ApplicationData::Current->LocalFolder`，而这两者在应用生命周期边缘（挂起/恢复/页面
  拆除）都可能为 null；`EnsureStarted` 挂在每次呈现唤醒上，必然撞上。项目 `/EHs-c-` +
  `_HAS_EXCEPTIONS=0`，`try/catch` 兜不住 SEH，已改为显式判空。
- **`ScheduledTask` 释放崩溃（SIGABRT）** —— `WTF::RedBlackTree::remove()` 只改别人的
  链接，**从不清被摘节点自己的 `m_left`/`m_right`**（它们是 `CheckedPtr`）。上游靠下次
  `insert()` 的 `reset()` 兜底，只对"移除后还会重插"的节点成立；`ScheduledTask` 是
  **移除→销毁**，陈旧引用留到删除即报 `1 CheckedPtr(s) outstanding`。已在 `remove()`
  返回前清除（`WK_WINUWP` 守卫）。
- **音频设备选择** —— 本机两个渲染端点（`Speakers` / `Internal Handset Speaker`）的
  `IsDefault` **全为 0**（Win10M 不填该属性），旧逻辑要求 `IsDefault` 才绑定 → 一个都没选
  → 退回静默的隐式绑定。改为 `IsDefault` 优先 → **非听筒名字优先** → 首个可用端点。

### 工具与工程

- **CI 新增补丁完整性检查** —— `WebKit/` 不在仓库，上游改动只能靠补丁入库；本项目**两次**
  因"补丁清单不全"而面临工作丢失（媒体 4 个新文件、上一轮 CheckedPtr 修复）。新增
  `tools/Test-PatchIntegrity.ps1`（清单一致 / 结构合法 / 反向应用）并接入 CI。
- **媒体补丁从 7 文件扩到 12 文件**（162 KB）—— 补录此前遗漏的 CheckedPtr 修复
  （`CheckedRef.h` / `Assertions.cpp` / `FastMalloc.h` / `RunLoopGeneric.cpp`）。
- **仓库根目录清理**：166 个文件 → 14 个（61 个 scratch 归档，152 个构建日志归档）。
- 摘除 DXVA 一次性探针（结论已存档，它还白搭 ~8 秒元数据延迟）。

## 真机验证状态

| 项 | 状态 |
|---|---|
| 设备机型 / 系统 | Lumia 950 / 15254.603 |
| appx 安装 | ✅ |
| `<video>` 画面 | ✅ 用户确认"视频能看" |
| `<video>` 声音 | ✅ **用户确认"听到声音了"** |
| 播放结束不再闪退 | ✅ 0.8.0.4 起 `crash.txt` 无新增该条目 |
| 软渲染底座无回归 | ✅ （GPU 未起时照常） |
| `ScheduledTask` 崩溃修复 | ⏳ 本版（0.8.0.6）待长时运行确认 |

## 资产

- `Harness_0.8.0.6_ARM.appx`
- `Harness_0.8.0.6_ARM.cer`

## 已知问题

- **Bilibili 等 MSE 站点无法播放** —— `ENABLE_MEDIA_SOURCE=OFF`，且 `supportsType()` 对非
  `FileOrHLS` 一律拒绝。已知路径：`MediaPlayer` + `AdaptiveMediaSource`（OS 原生 DASH）；
  研究任务书见 `docs/TASK-HWDECODE-MSE.md`。
- **硬件解码仍为软件路径** —— DXVA 此前判定"此路不通"**可能下得过早**：从未调用
  `ID3D11Multithread::SetMultithreadProtected(TRUE)`，而缺失它正是 `E_FAIL` 的典型成因。
  待重审（同上任务书）。
- **`ENABLE_TOUCH_EVENTS=OFF`** —— 网页收不到真 `touchstart`，会走桌面分支。
  用户体验改进清单见 `docs/UX-RESEARCH-2026-10-23.md`。
- **清晰度默认 720p** —— 在 1440×2560 的屏幕上偏保守；可手动切 1080p/原生。

## 升级注意

- 依赖 `Microsoft.VCLibs.140.00 (ARM)`（设备多半已随其他 -Reborn 应用装上）
- 从更早版本升级时 LocalState 数据保留；残留的 `gpu-crash.flag` 会自动降级软件渲染一次
- 设置页可调：清晰度（720p/1080p/原生）、JIT 模式、GPU 渲染、页面宽度、轴锁定、双击缩放
