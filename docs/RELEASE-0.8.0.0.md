# Release 0.8.0.0 — 发布清单

> 状态：**最终发布准备中**
> 版本：`0.8.0.0`　分支：`media-mf`　日期：2026-10-20
> 本文是发布执行清单；发布后重点转向**视频/音频**与**体验优化**。

---

## 1. 版本与分支

| 项 | 值 |
|---|---|
| App manifest 版本 | `0.8.0.0` |
| 目标 tag | `v0.8.0.0` |
| 分支 | `media-mf`（GitHub 默认分支） |
| 引擎基线 | `webkitgtk-2.54.0`（`wk-winuwp.patch`） |
| 目标设备 | Lumia 950 / 950 XL，Windows 10 Mobile 15254，ARM32 |

---

## 2. 发布范围

本版本是**最终发布准备基线**，包含：

- WebKit 2.54.0 ARM32/UWP 移植（WTF / JavaScriptCore / WebCore）
- Cairo 软件渲染 + ANGLE/TextureMapper GPU 合成
- Lumia 920/930（WDDM 1.x）自动降级
- Media Foundation H.264 软解；音频接线进行中
- 浏览器宿主：标签页、地址栏、书签、历史、下载、设置、页内查找、中英俄 UI
- TileGrid v2、交互完善（confirm/prompt/select/color/date/复制粘贴）

**不在本版本范围**：MSE、Web Audio、WebRTC、DXVA 硬解、JIT 出货启用。

---

## 3. 构建

```powershell
# 增量构建（推荐；要求 build-clang-gpu 已配置）
pwsh -File E:\Apotheosis\port\build-0.8.0.0.ps1

# 全新配置后构建
pwsh -File E:\Apotheosis\port\build-0.8.0.0.ps1 -Configure
```

产物：

```
harness\AppPackages\Harness\Harness_0.8.0.0_ARM_Test\Harness_0.8.0.0_ARM.appx
```

> 本机 16 GB：ninja 必须 `-j 2`；全量 WebCore 约 1h45m。

---

## 4. 真机验证矩阵

| # | 项 | 通过标准 | 证据 |
|---|---|---|---|
| 1 | 冷启动 | 3 秒内出首屏 | stage.txt timeline |
| 2 | 软渲染 | Bing / GitHub / Apple 正常 | 截图 + console.txt |
| 3 | GPU 合成（950） | 平滑滚动 + 捏合缩放 | gpu-gate 行 + 截图 |
| 4 | 920/930 降级 | 自动走 Cairo，不闪退 | gpu-gate 行 |
| 5 | 视频 | H.264 页面可播放 | media 行 |
| 6 | 音频 | 有声音（若本轮完成） | mediaaudio.txt |
| 7 | 交互 | 点击/滚动/输入/选择/confirm/prompt | 截图 |
| 8 | 网络 | HTTPS / Cookie / WebSocket | server.log |
| 9 | 稳定性 | 30 分钟无闪退 | crash.txt 为空 |

**未在真机验证的项不得在 release notes 中标记为通过。**

---

## 5. 发布资产

| 资产 | 来源 |
|---|---|
| `Harness_0.8.0.0_ARM.appx` | `port/build-0.8.0.0.ps1` |
| `Harness_0.8.0.0_ARM.cer` | 构建机签名证书 |
| `Microsoft.VCLibs.ARM.12.00.Phone.appx` | 设备依赖（可选附带） |

---

## 6. 发布流程

1. 确认 `harness/Package.appxmanifest` 版本 = `0.8.0.0`。
2. 真机跑完第 4 节矩阵，证据归档。
3. 打 tag 并推送（触发 `.github/workflows/release.yml`）：

   ```bash
   git tag v0.8.0.0
   git push origin v0.8.0.0
   ```

4. 从构建机附上资产：

   ```bash
   gh release upload v0.8.0.0 Harness_0.8.0.0_ARM.appx Harness_0.8.0.0_ARM.cer --clobber
   ```

5. 在 release notes 中如实填写"已验证 / 未验证 / 已知限制"。

---

## 7. 已知限制（必须写进 release notes）

- JSC JIT 实验性，出货构建未启用。
- `<video>`/`<audio>` 声音输出未完成。
- MSE / Web Audio / WebRTC 未开始。
- DXVA 硬解在 App Container 内不可用。
- 老设备（920/930）只能软件渲染。

---

## 8. 回滚

- 若新版本在真机出现阻断问题：`gh release edit v0.8.0.0 --draft` 并附说明；
- 设备侧可回装上一版 appx（同 Publisher，允许降级需先卸载）；
- 保留 `harness/AppPackages/` 下上一版产物与签名证书。

---

## 9. 发布后路线图

### 9.1 视频 / 音频（最高优先级）

- 音频输出：AudioGraph / MF 音频渲染器接线，端点选择与音量；
- 播放生命周期：暂停/恢复、seek、缓冲、`ended`、错误恢复；
- 音画同步与后台挂起行为；
- 可选：MediaPlayer + VideoFrameAvailable 路线评估。

### 9.2 体验优化

- 滚动/缩放手感（TileGrid 调参、惯性、边缘回弹）；
- 页面加载与首屏时间；
- 内存占用与功耗（UWP 内存压力回调、缓存策略）；
- UI 细节：地址栏、标签页、设置、多语言。

### 9.3 暂不计划

- DXVA 硬解（已证伪）；
- MSE / Web Audio / WebRTC（需另立架构）；
- JIT 出货启用（需完成 2.54 的 32 位 JIT 适配）。
