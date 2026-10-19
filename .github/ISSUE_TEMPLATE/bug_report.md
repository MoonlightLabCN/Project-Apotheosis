---
name: 缺陷报告 / Bug report
about: 真机或构建机的缺陷——带证据提工单，肉眼描述无法定位
title: "[BUG] "
labels: bug
---

## 现象

（一句话：看到什么，预期是什么）

## 环境

- **设备机型**：Lumia 950 / 950XL / 930 / 920 / 其他：____
- **系统版本**（Device Portal 首页或 stage.txt 里）：____
- **App 版本**（Package.appxmanifest 的 Version）：____
- **复现页面 / URL**：____

## 证据（本项目的工单**没有证据一律关闭**）

真机问题请附上 WDP 拉回的 `LocalState\` 文件（`tools\pull-file.ps1` 可拉）：

- [ ] `stage.txt`（含 `media` / `media-state` / `gpu-gate` / `timeline` 行）
- [ ] `crash.txt`（若有闪退；注意崩溃时间戳）
- [ ] `console.txt`（页面 JS 自报）
- [ ] LAN 测试服务器日志（`port\_mediatest\server.log`，媒体线问题必附）
- [ ] `mediaaudio.txt`（音频问题必附）

```
（粘贴关键行）
```

## 复现步骤

1. …
2. …

## 已排除的可能

（参考 `docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md` 的"被证伪的假设"表，避免重复踩坑）
