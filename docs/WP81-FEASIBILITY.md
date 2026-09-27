# WP8.1 可行性研究（2026-09-27）

问题：Win10M 版（Harness_0.2.0.0_ARM）这套东西能不能让 **Windows Phone 8.1** 也用上。
结论：**技术上可行，但是一次完整再移植（月级工程），不是适配补丁。**
引擎核心可复用度高，卡点集中在工具链/STL、平台 API 面、宿主三层。

## 一、三层逐项评估

### 1. 引擎核心（WTF/JSC/WebCore + curl/Cairo/字体）—— 可复用度高
- 现有 WK_WINUWP 守卫本来就是按"App Container 可用 API 子集"写的，WP8.1 的
  WinPRT API 面是另一个（更窄的）子集，守卫策略可平移；新增一批 `WK_WP81` 分支即可。
- 传输层自带 curl+OpenSSL（WinSock 在 WP8.1 子集内），不依赖系统网络栈 ✓
- 软渲染（Cairo→RGBA→WriteableBitmap）路径完全不依赖 Win10 特有 API ✓
- **JIT 在 WP8.1 基本无望**：VirtualAlloc(PAGE_EXECUTE*) 不在手机应用 API 面里，
  可执行内存策略比 Win10M 更严。LLInt（当前构建的配置！）恰好是适配形态 ✓

### 2. 工具链 / STL —— 最大的硬骨头
- WP8.1 = VS2013 代工具集（v120_wp81）、**msvcr120 代 CRT/STL（C++11 级）**。
  WebKit 2.54 需要 **C++20 STL**（std::span / std::variant / concepts），
  WP8.1 的 STL 提供不了 → 必须 **捆绑 libc++**（clang-cl + libc++ + 手机 CRT 底座）。
  这是"Node.js 上奇异平台"的同款方案，可行但是实打实的工程量。
- clang-cl 以 thumbv7-windows-msvc 对 WP8.1 SDK 头/库编译，原理可行（未验证）。
  验证顺序建议：先拿 libc++ 的最小 TU 在 WP8.1 SDK 下编过 —— 这是 gating 实验。

### 3. 平台 API 面 —— 中度改造
- **SwapChainPanel 是 Windows 10 专属**（文档：Device family Windows 10,
  introduced 10.0.10240.0）。WP8.1 只有 **SwapChainBackgroundPanel**（8.0 代
  XAML/DirectX 互操作，**只能做视觉树根元素**）→ GpuPanel 嵌在 Grid 里的现有
  布局要重构为"背景板为根、UI 叠在子节点"的结构。
- ANGLE 的 WindowsStore 后端本来就是 8.1 Store 生态产物（D3D11 FL9_3 正是
  WP8.1 的档位），GPU 层是希望最大的一层 ✓（本地 angle\arm 的预编译件可先
  用 8.1 SDK 重链验证）。
- OSAllocator/VirtualAlloc、GlobalMemoryStatusEx、K32GetProcessMemoryInfo、
  GetProcAddress 动态解析等要换成 WP81 分区可用物（malloc 化 + WinRT API）。
- SQLite 的 CreateFileW 系 App-Container VFS 补丁要按 WP8.1 的文件 API 面再调。

### 4. 宿主 harness —— 重写量最大
- C++/CX XAML 用的是 Win10 合同（InputPane/ApplicationView/CoreApplication 等
  8.1 有同类但签名/可用面不同）；MainPage.xaml.cpp 5000+ 行按 8.1 合同过一遍。
- 打包/部署：WP8.1 的 appx 清单 schema 不同（Phone 合同/能力声明）；
  商店已死，但开发者解锁 + Application Deployment 工具可以侧载 ✓

## 二、设备现实
WP8.1 专属机型 = 升不上 W10M 的那批：Lumia 520/525/620/625/630/635/720/730/735/
820/920/925/1020/1320/532/535/540/430/435 等。内存 512MB–1GB：
- **512MB（520/620/630/532…）**：本 port 在 3GB 的 Win10M 上都经历过 OOM 血战，
  512MB 跑完整 WebKit 极其凶险，不建议作为目标。
- **1GB（920/925/1020/720/1320/820…）**：可谈。LLInt（无 JIT）反而省内存；
  JSC 堆上限、图片解码缓存都要按 1GB 重新调参。

## 三、工期与建议
按层估计：libc++/工具链打通 1–2 周；API 面适配（OSAllocator/文件/内存诊断）约 1 周；
宿主按 8.1 合同重写 2–3 周；真机调试不可估。**月级**。

建议路径（若推进）：
1. **Gating 实验**：clang-cl + libc++ + WP8.1 SDK 编过一个用 std::span 的最小 TU；
   不过则整个计划不成立，一天内可证伪。
2. 引擎 LLInt + 软渲染线先上（不碰 GPU），在 1GB 机型出"能浏览"的最小可用版。
3. GPU 线后置：ANGLE 8.1 重链 + SwapChainBackgroundPanel 布局重构。
4. 宿主最后做（工作量最大但无技术风险）。

旁注：同一份评估对 **Windows RT 8.1**（Surface RT/Lumia 2520 等 ARM32 平板）几乎
1:1 适用，RT 的 API 面反而宽一点，可作顺带目标。
