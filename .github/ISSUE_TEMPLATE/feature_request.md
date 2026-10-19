---
name: 功能请求 / Feature request
about: 新能力或移植缺口——提之前先看交接单的 DoD 表和"未开始"清单
title: "[FEAT] "
labels: enhancement
---

## 想要什么

（一句话）

## 为什么 / 谁需要

（真实站点场景？哪个 DoD 项？）

## 已知的上游/平台约束

（例如：MSE 需要 `parameters.platformType != FileOrHLS` 支持；硬件解码已结论不通，见 `docs/HANDOFF-VIDEO-AUDIO-2026-10-13.md` §12）

## 验收标准

- [ ] 真机（Lumia 950, 15254）可复现
- [ ] 有 stage.txt / 服务器日志级证据
- [ ] 不回软软渲染底座（GPU 路失败时零回归）
