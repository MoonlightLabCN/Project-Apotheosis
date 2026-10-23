# 用户体验优化研究：触屏 / 渲染质量 / 动画质量

**日期**：2026-10-23
**分支**：`fix/scheduled-task-checkedptr`（研究文档，实施另开分支）
**现状基线**：0.8.0.6（真机已验证：画面 ✅ 声音 ✅ / 触屏交互可用 / ANGLE GPU 合成可用）
**目标设备**：Lumia 950（WQHD 1440×2560，DIP 360×640，scale 4.0，ARM32 双核 A57 @1.8GHz）

---

## 0. 结论速览

> ⚠️ **2026-10-23 用户反馈:设备"贼卡"。** 这改变了优先级 —— **卡顿是第一优先级**,清晰度/动画
> 的美化都要让位。而且它直接否定了"提高默认清晰度"这个方向:见 §2.2 R1 的订正。

三块都有**明确的、低成本高回报的改进点**，且大多不需要动引擎：

| 领域 | 最高价值改进 | 成本 | 预期收益 |
|---|---|---|---|
| **触屏** | 打开 `ENABLE_TOUCH_EVENTS`（真触摸事件）+ 惯性手感调参 | 中（回归面需评估） | 站点走移动分支、自定义手势可用 |
| **渲染质量** | 清晰度档位默认值与自适应 | 低 | WQHD 屏幕白瞎 → 文字锐利 |
| **动画质量** | 帧节奏从"事件驱动+兜底"到"动画期固定 60Hz" | 中 | 动画从卡顿到流畅 |

**共同瓶颈**：这台机器是 **ARM32 双核 @1.8GHz**，GPU 是 Adreno 430。所有优化必须在
**功耗 / 帧率 / 清晰度**三角里做取舍，且**必须有真机读数**（不能只靠推测）。

---

## 1. 触屏优化

### 1.1 现状（已实现，相当完整）

**Harness 侧**（`harness/MainPage.xaml.cpp`）：

| 机制 | 位置 | 说明 |
|---|---|---|
| `ManipulationDelta/Completed` | L1273+ | 滚动/缩放主路径（XAML 手势） |
| `ManipulationStarted` 手工订阅 | L1277 | XAML 标记里没钩子，代码补 |
| 双击缩放 | L1013 / `kDoubleTapSlopPx=60` | 含"路线修复 2026-09-10" |
| 长按 | `kLongPressEngineHoldMs=600` | **有真机 A/B 记录**（600ms 是踩出来的） |
| 轴锁定 | `SetAxisLockSwitch` | 单指滚动吸附方向（设置项已有） |
| 翻页按钮 | `SetScrollFabSwitch` | 可达性 |

**引擎侧**（`port/WebCoreDriver.cpp`）：`WebCoreClickAt` / `ClickAtCount` / `ScrollBy` /
`WheelAt` / `DragAt` / `LongPressAt` / `ZoomWheelAt` / `SetPageScale`，且**拖拽已改为
派发 pointer events**（L713 注释：「a mousedown … every gesture that reaches WebCoreDragAt
came from a finger」）。

### 1.2 缺口与改进点

| # | 问题 | 证据/推断 | 建议 |
|---|---|---|---|
| T1 | **`ENABLE_TOUCH_EVENTS=OFF`** | `CMakeCache.txt` 实测 | 打开后网页能收到真 `touchstart/touchmove/touchend`，**很多站点靠它做自定义手势**（地图、轮播、编辑器）。当前靠 mouse 事件模拟，站点会走桌面分支 |
| T2 | ~~无惯性滚动~~ **已实现，勿重复造** | `MainPage.xaml` L335 `ManipulationMode="TranslateX,TranslateY,TranslateInertia,Scale"` + `MainPage.xaml.cpp` 有完整的惯性期处理（L4451+ 注释记录了"合成减速曲线"与 `m_scrollLagFlingMoves` 埋点） | **修正**：惯性由 XAML 平台提供（`IsInertial` 分支）。可选优化是**调参**（减速曲线手感），不是从零实现 |
| T3 | **`ENABLE_ASYNC_SCROLLING=OFF`** | 实测 OFF | 异步滚动 = 滚动不等主线程。开启需协调图形路（我们是 TextureMapper，需评估） |
| T4 | 滚动条/触摸区域细节 | `ENABLE_TOUCH_EVENT_REGIONS`/`TOUCH_ACTION_REGIONS` 在 MetroSurf WP81 有定义 | `touch-action` 支持影响手势与页面滚动的分工 |
| T5 | 手势优先级冲突 | 长按 600ms vs 地图拖拽（L1079 注释已记录此冲突） | 已有缓解；可再看"拖拽早期取消长按" |

### 1.3 触屏研究待办

- [ ] 真机验证：`touchstart` 是否存在于页面（`'ontouchstart' in window`）
- [ ] 测当前滚动是否"跟手"（手指移动 vs 内容移动的延迟）
- [ ] **惯性手感调参**（已有实现；取真机 `fling=` 埋点读数后决定是否调减速曲线）
- [ ] 评估 `ENABLE_TOUCH_EVENTS` 打开后的回归面（现有 mouse 模拟路径会变）

---

## 2. 渲染质量

### 2.1 现状

**清晰度档位**（`MainPage.xaml.cpp:899+`）——**已实现，三档**：

```
0 = 720p(默认,最省电)
1 = 1080p
2 = 原生(短边 × CurrentDeviceScale)
```
设置项 `SetClarityCombo` 已暴露。注释写明「合成与内存开销随像素数平方涨」。

**DPR / 缩放模型**（`port/WebCoreDriver.cpp:360+`）：
```
page.setDeviceScaleFactor(wkPageWidthFactor())   // 页面宽度因子 = 设备缩放因子
contentsScale = pageScaleFactor * deviceScaleFactor
```
即：**布局视口按 CSS px（手机尺寸），栅格按引擎像素**（保清晰）。这套设计是对的。

**GPU 合成**：ANGLE(D3D11 FL9_3) + TextureMapper，`g_gpuActive` 运行时门控；
软渲染 Cairo 是通用底座（GPU 失败零回归）。

### 2.2 缺口与改进点

| # | 问题 | 证据 | 建议 |
|---|---|---|---|
| R1 | ~~默认 720p 浪费 WQHD 屏~~ **已订正** | 试过把默认改 1080p，但用户反馈设备本就"贼卡"，而合成与内存按**像素数平方**涨（1080p ≈ 720p 的 2.25 倍工作集） | **保持 720p 默认**。清晰度不是免费的质量提升，是**直接拿流畅度换锐度**。要提清晰度应做成**自适应**：按实测帧耗时决定，而不是按屏幕像素数 |
| R2 | 档位切换需重启 | 注释：「`m_engineScale` 在首帧锁定，运行期改设置不影响本会话」 | 做成**免重启热切换**（触发重栅格+重建 tile） |
| R3 | 字体渲染质量未知 | `USE_SKIA=ON`？我们是 Cairo/Skia 需确认 | 真机对比字号/字重/抗锯齿，必要时调 font hinting |
| R4 | 图像解码质量 | 无 AVIF/WebP 记录 | 确认 WebP/AVIF 支持（现代站点大量用） |
| R5 | 颜色/对比度 | 未见 color profile 处理 | 评估 sRGB 处理与暗色模式 |
| R6 | 缩放后的文字锐利度 | `contentsScale` 机制已对 | 真机验证 pinch 放大后文字是否仍锐（tile 重栅格时机） |

### 2.3 渲染质量研究待办

- [ ] 真机同页对比 720p / 1080p / 原生：文字锐度、滚动流畅度、功耗
- [ ] 测各档的**内存占用与合成耗时**（决定默认值）
- [ ] 确认当前字体后端（Cairo vs Skia）与 hinting 设置
- [ ] 确认 WebP/AVIF 支持状态
- [ ] 设计"自适应清晰度"（接 `MemoryManager` 事件，已有内存压力逻辑可复用）

---

## 3. 动画质量

### 3.1 现状（关键：**事件驱动，无固定帧率**）

`MainPage.xaml.cpp:5225` 注释直言：

> **事件驱动：没有固定帧率。** 兜底 tick 负责自愈 + 内存采样 + 补没被信号覆盖的变化，
> 刚有活动时按 200ms，画面静下来后 1s，再静下来 5s。

`ScheduleWakeComposite()` 的限流逻辑（**已调优过一轮**）：

```cpp
unsigned minGap = 16;                                    // ≈60Hz 下限
if (m_lastPresentDurMs > minGap) minGap = (m_lastPresentDurMs > 50) ? 50 : m_lastPresentDurMs;
if (m_liveTotalTicks >= 150) minGap = 250;               // 失控动画兜底 4fps(原为 1fps)
```

注释记录了历史问题：**旧的占空比规则让慢帧把间隔翻倍**（100ms 合成 → 200ms/帧 ≈5fps）。
已修为"慢帧追平上限 50ms（≥13fps）"。

**引擎侧**：`WebCoreLiveTick` 推进 + 合成；`PortChromeClient::triggerRenderingUpdate`
置 needsPresent；`g_gpuAnimating` 记录图层树是否仍有动画（`applyAnimationsRecursively`）。

### 3.2 缺口与改进点

| # | 问题 | 证据 | 建议 |
|---|---|---|---|
| A1 | **动画期无固定 60Hz** | 注释明说"没有固定帧率"；`minGap=16` 只是下限 | 动画进行中（`g_gpuAnimating==true`）用**固定 16.6ms** 节奏，而非"跟着上一帧耗时走" |
| A2 | 兜底 tick 200ms 太慢 | `fb.Duration = 2000000LL`（200ms） | 有动画时应有更快的兜底（兜底本意是自愈，不该承担动画） |
| A3 | 失控动画降速过快 | 150 帧（≈30s）→ 250ms | 区分"真失控"与"长动画"（如视频背景、CSS 循环动画） |
| A4 | CSS 动画/过渡的帧率 | 未验证 | 真机测 CSS transition / transform 动画的流畅度 |
| A5 | ~~合成耗时未知~~ **已有** | `PerfRow` 有 63 列:renderUpdate/flush/backing/paint/readback/swap/blit + dirty 统计 + raster_* + 网络分解 | **修正**:埋点早就存在且设置页可开。缺的是**分析器**,已补 `tools/Analyze-Perf.ps1` |
| A6 | XAML 侧动画与引擎动画叠加 | 项目有多处 XAML `Duration`/`Storyboard`（标题栏滑动、缩放弹簧） | 评估重叠期的掉帧 |

### 3.3 动画质量研究待办

- [ ] 真机测帧率：CSS transform 动画、`requestAnimationFrame` 循环、CSS transition
- [x] ~~帧耗时埋点~~ 已有；已补分析器 `tools/Analyze-Perf.ps1`（按阶段排序 + 栅格健康度 + 结论）
- [ ] 实现"动画期固定节奏"（A1）并 A/B
- [ ] 评估"失控动画"判据是否误伤长动画（A3）

---

## 4. 三块的共同约束（必须先立规矩）

1. **真机读数优先**：本项目已多次因"文档推测代替真机"走弯路（DXVA 结论、AudioGraph 结论、
   JIT 归因）。任何 UX 改进都要有**真机前后对比**。
2. **功耗是硬约束**：950 是 2015 年机器。60Hz + 1080p + GPU 合成三者叠加会显著发热降频。
   **必须有温度/电量读数**才能定默认档。
3. **软渲染是通用底座**：GPU 路失败必须零回归（`g_gpuActive` 门控，现有设计要保持）。
4. **920/930（WDDM 1.x）走软件路**：任何 GPU 相关优化不能假设 ANGLE 一定可用（设备门已有）。
5. **改动要有开关**：清晰度已有档位；触屏与动画的新机制也应可关闭（真机 A/B 与降级都需要）。

---

## 5. 建议的实施顺序

**第一步（低风险高回报，先做）**
1. **清晰度默认 1080p + 自适应**（R1/R2）—— 纯设置逻辑，真机一测就知道
2. ~~帧耗时埋点~~ **已有**;改用 `tools\Analyze-Perf.ps1` 分析真机 perf.csv,先定位卡的阶段 —— **这是当前第一优先**

**第二步（手感，中风险）**
3. **惯性手感调参**（T2）—— 已实现,取真机 `fling=` 埋点后决定是否调减速曲线
4. **动画期固定节奏**（A1/A2）—— 有了埋点数据后再动

**第三步（完整度，需评估回归面）**
5. **`ENABLE_TOUCH_EVENTS`**（T1）—— 会改变现有 mouse 模拟路径，需充分回归
6. 字体/图像质量细节（R3/R4）

**暂不建议**
- `ENABLE_ASYNC_SCROLLING` / `USE_COORDINATED_GRAPHICS`（T3）—— 我们是 TextureMapper 路，
  换协调图形是大手术，且 MetroSurf 那套是**互斥的另一条架构线**（它关 MF、静态库、
  `_WINDOWS`），不该为此推倒已验证的底座。

---

## 6. 与硬解/MSE 任务的关系

**有耦合但可并行**：
- 硬解（`docs/TASK-HWDECODE-MSE.md` 支线 A）若走 `MediaPlayer` 帧服务，**视频呈现路径会变**，
  届时渲染质量（尤其视频缩放质量）需一并复测
- MSE（支线 B）成功后，Bilibili 的动画/交互压力会上升，**动画质量优化会更重要**
- **清晰度与帧节奏是独立轴**，可先做，不受硬解进度影响

---

## 7. 待补的真机读数（研究的第一步就是这个）

| 项 | 怎么取 |
|---|---|
| 当前帧率 | 帧耗时埋点（需先加）或 `perf` 日志（`SetPerfLogSwitch` 已存在） |
| 清晰度各档差异 | 同页截图对比 + 内存读数 |
| 触屏延迟 | `touchstart` 时间戳 vs 内容移动 |
| 功耗/温度 | 播放 10 分钟视频 / 滚动 10 分钟，记录电量曲线与体感温度 |
| 内存压力 | `stage.txt` 已有内存字段 + `MemoryManager` 事件日志 |
