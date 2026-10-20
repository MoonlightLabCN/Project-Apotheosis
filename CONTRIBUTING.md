# CONTRIBUTING.md — 参与 Project Apotheosis

感谢你对 Windows 10 Mobile / WebKit 移植的兴趣。本文件说明本仓库的协作方式、
工程铁律与 PR 要求。**提交前请先读 [`AGENTS.md`](AGENTS.md)。**

---

## 1. 先读什么

| 文档 | 为什么 |
|---|---|
| [`AGENTS.md`](AGENTS.md) / [`CLAUDE.md`](CLAUDE.md) | 完整工程约束（三套工具链、ASCII 路径、异常关闭、版本纪律） |
| [`README.md`](README.md) | 项目现状、架构、已验证能力 |
| [`docs/CAPABILITIES.md`](docs/CAPABILITIES.md) | 能力 / 接缝 / 风险面 |
| [`docs/HARNESS-BUILD.md`](docs/HARNESS-BUILD.md) | 构建体系（含手搓 XAML 工具链） |

---

## 2. 分支模型

| 分支 | 角色 | 接受 PR？ |
|---|---|---|
| `media-mf` | **主开发线（默认分支）** | ✅ 是 |
| `gpu-path1` | GPU 合成历史线 | ❌ 存档只读 |
| `master` | 纯 JIT 历史线 | ❌ 存档只读 |

**所有 PR 目标分支必须是 `media-mf`。** 请从 `media-mf` 开功能分支，例如：

```bash
git switch media-mf
git pull
git switch -c feat/tilegrid-deadline
```

---

## 3. 工程铁律（违反必踩坑）

1. **ASCII 路径**：仓库必须在 `E:\Apotheosis`，vcpkg 在 `C:\vcpkg`。构建链
   （Ruby 代码生成器 / meson）对非 ASCII 路径敏感。
2. **三套工具链不能混**：
   - WebKit 引擎：clang-cl，`--target=thumbv7-unknown-windows-msvc`
   - `port/*.cpp`：clang-cl + lld-link
   - `harness/`：MSVC v143 ARM 14.44.35207 + SDK 22621（XAML codegen 另用 VS2017 v141）
3. **C++ 异常必须关**：`_HAS_EXCEPTIONS=0` + `/EHs-c-`。
4. **所有 WebKit 改动**用 `#if defined(WK_WINUWP)` 守卫，并带 `Apotheosis:` 注释。
5. **改完 `WebKit/` 必须重新生成补丁**：`patches/2026-10-09-winuwp-media-mf.patch`。
6. **改 `port/` 或 `harness/` 必须升 `harness/Package.appxmanifest` 的 Version**。
7. **C ABI 两份头必须逐字节一致**：`port/WebCoreDriver.h` 与 `harness/WebCoreDriver.h`。
8. **同版重装是静默 no-op**：版本号不升，设备会继续跑旧二进制。

---

## 4. 提交信息

使用 Conventional Commits 风格：

```
<type>(<scope>): <subject>

<body: 为什么这么做，而不是只写做了什么>
```

常用 `type`：`feat` / `fix` / `docs` / `chore` / `ci` / `refactor` / `test` / `perf`。
`scope` 可用 `media` / `jit` / `gpu` / `harness` / `port` / `tools` / `repo`。

示例：

```
fix(media): 播放中 readyState 不再被每帧打回 HAVE_CURRENT_DATA

根因：notifyFrameReady 每帧无条件重置 readyState，导致 waiting/stalled
反复触发。改为仅在状态真正变化时上报。
```

---

## 5. PR 要求

提交 PR 前请逐项确认（CI 会强制其中一部分）：

- [ ] 目标分支是 `media-mf`，一个 PR 只做一件事
- [ ] 改了 `port/` 或 `harness/` → 版本号已升
- [ ] 改了 C ABI 导出 → 两份头逐字节一致
- [ ] 改了 `WebKit/` → 补丁已重新生成且 `git apply --check -R` 通过
- [ ] 所有 `.ps1` 通过语法解析
- [ ] 无新增非 ASCII 路径/文件名
- [ ] 涉及引擎/壳行为的变化 → 附真机证据（`stage.txt` / `crash.txt` / 服务器日志）
- [ ] 软渲染底座无回归（GPU 未起时页面照常）

模板见 [`.github/PULL_REQUEST_TEMPLATE.md`](.github/PULL_REQUEST_TEMPLATE.md)。

---

## 6. Issue 要求

- **缺陷报告**：必须附环境和证据（`stage.txt` / `crash.txt` / `console.txt` /
  `server.log` / `mediaaudio.txt`）。没有证据的缺陷报告很难定位。
- **功能请求**：先看 `docs/CAPABILITIES.md` 的"未开始"清单与交接单的 DoD 表，
  避免重复。
- **安全问题**：不要公开提 Issue，见 [`SECURITY.md`](SECURITY.md)。

模板见 [`.github/ISSUE_TEMPLATE/`](.github/ISSUE_TEMPLATE/)。

---

## 7. 测试

```powershell
# 主机 PSL 单测（x64，秒级）
pwsh -File .\tests\psl\run-psl-tests.ps1

# 设备 Web 平台测试（真机手动打开）
python .\tests\web-platform\server.py
```

改动涉及引擎/驱动时，必须部署到真机验证；x64 构建机无法运行 ARM32 appx。

---

## 8. 贡献授权

本项目以 **MIT License** 发布（见 [`LICENSE`](LICENSE)）。提交 PR 即表示你同意
你的贡献以 MIT License 授权，并确认你有权提交该代码。

第三方代码请勿直接粘贴；如确需引入，请在 PR 中注明来源与许可证，并同步更新
[`NOTICE`](NOTICE)。

---

## 9. 行为准则

参与本项目即表示同意遵守 [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md)。
