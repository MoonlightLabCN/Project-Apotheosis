# Security Policy

## 报告安全问题

**请不要通过公开 Issue 报告安全漏洞。**

请通过以下方式私下联系维护者：

- 在 GitHub 上使用 **Security → Report a vulnerability**（推荐，若仓库已启用）；
- 或通过维护者公开资料中的邮箱联系。

请在报告中尽量包含：

- 受影响的版本 / 提交；
- 复现步骤或 PoC；
- 影响范围（例如：沙箱逃逸、证书校验绕过、内存破坏、任意文件读写）；
- 是否已在真机验证。

## 响应

项目为个人维护的研究项目，响应时间可能不固定。维护者会在确认后尽快评估并
在后续版本中修复；修复前请勿公开细节。

## 范围

本项目包含：

- `port/`：WebCore 驱动与平台适配层；
- `harness/`：UWP 宿主；
- `patches/`、`wk-winuwp.patch`：对 WebKit 的修改。

以下**不在**本项目范围内（请向上游报告）：

- WebKit 上游本身的漏洞（<https://bugs.webkit.org>）；
- Windows / Media Foundation / ANGLE 的漏洞（向 Microsoft / Google 报告）。
