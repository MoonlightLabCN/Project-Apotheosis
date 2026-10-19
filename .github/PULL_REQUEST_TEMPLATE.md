# PR 检查清单

> 本仓库的每条纪律都是真机/构建机上踩出来的。提交前逐项确认，CI 会替你守住其中五项。

## 分支与范围

- [ ] 目标分支是 `media-mf`（`gpu-path1`/`master` 为存档线，只读）
- [ ] 改动范围与提交信息一致（一个 PR 一件事）

## 纪律清单（CI 强制）

- [ ] **改了 `port/` 或 `harness/` → `harness/Package.appxmanifest` 的 Version 已升**（同版重装是静默 no-op，设备会继续跑旧二进制）
- [ ] **改了 C ABI 导出 → `port/WebCoreDriver.h` 与 `harness/WebCoreDriver.h` 两份逐字节一致**
- [ ] **改了 `WebKit/` → 已重新生成 `patches/2026-10-09-winuwp-media-mf.patch` 且 `git apply --check -R` 通过**（`WebKit/` 不在仓库，未入库的改动会丢）
- [ ] 所有新增/修改的 `.ps1` 通过语法解析（CI 检查）
- [ ] 无新增非 ASCII 路径/文件名（构建链对非 ASCII 敏感）

## 真机验证（涉及引擎/壳行为的变化必填）

- [ ] 设备机型与系统版本：____
- [ ] appx 已装到真机（版本号与 manifest 一致）：____
- [ ] 证据：`stage.txt` / 服务器日志 / `crash.txt`（贴关键行）

```
（贴证据）
```

## 自测

- [ ] `pwsh -File port\build-harness.ps1` 通过（改了 harness）
- [ ] `pwsh -File port\link-driver-gpu.ps1` 通过（改了 driver；注意本机 `ninja -j 2`，`-j 4` 会 OOM）
- [ ] 软渲染底座无回归（GPU 未起时页面照常）
