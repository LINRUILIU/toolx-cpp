# toolx-http 产品设计记录

> Audience: ToolX 维护者
> Status: Historical design evidence
> Applies to: `toolx-http` 进入 0.3.0 产品链之前的设计阶段
> Source of truth for: 历史产品动机；当前合同以公开 CLI reference 为准

## 产品动机

配置合成和打包之后，发布/部署流程需要一个很小的已安装命令验证 health、
readiness 与简单 HTTP contract。`toolx-http` 因此定位为 runtime endpoint
preflight，而不是把 `httpx` 的全部能力重新包装成 `curl`。

## 设计场景

- 单 endpoint 与 manifest batch；
- status 或 status range 断言；
- 简单 body substring 断言；
- 小型 header/body 请求；
- timeout、retry、redirect 控制；
- 适合 CI/release smoke 的稳定 JSON；
- 可选 audit log。

## 组合模块

| 模块 | 用途 |
| --- | --- |
| `argtool` | 参数、help、usage error |
| `cfgx` | manifest 与 JSON envelope |
| `schemax` | manifest 未知字段拒绝 |
| `httpx` | 请求、retry、redirect、timeout、redaction |
| `logsys` | 可选审计日志 |

设计阶段决定不增加新的公共 C++ 模块。

## 非目标

load test、OAuth、下载/上传、复杂 JSONPath/regex/schema assertion、证书管理
与外网依赖 release smoke 均不进入 MVP。

当前公开规范见 [`docs/cli/toolx-http.md`](../../cli/toolx-http.md)。
