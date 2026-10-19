# Release v{VERSION}

**日期**：{YYYY-MM-DD}　**分支**：`media-mf`　**提交**：{SHA}

## 本版内容

（按"修复/新能力/工具"分组，每条一行；根因类修复附一句"为什么"，不只写"修了什么"）

## 真机验证状态

| 项 | 状态 |
|---|---|
| 设备机型 / 系统 | Lumia 950 / 15254.603 |
| appx 安装 | ✅ / ❌ |
| 关键证据（stage.txt / 服务器日志 / crash.txt） | （贴关键行） |
| 软渲染底座无回归 | ✅ / ❌ |

## 资产（从构建机手动附上）

- `Harness_{VERSION}_ARM.appx`
- `Harness_{VERSION}_ARM.cer`

> App 内『检查更新』靠 GitHub release 识别——**必须升版本号 + 建 release + 附 appx**，三者缺一不可。
> 覆盖同版资产用 `gh release upload v{VERSION} <appx> --clobber`。

## 已知问题

（诚实列出：未验证项、平台限制、下一步）

## 升级注意

- VCLibs 140.00 (ARM) 依赖（设备多半已随其他 -Reborn 应用装上）
- 从更早版本升级时，LocalState 数据保留；`gpu-crash.flag` 残留会自动降级软件渲染一次
