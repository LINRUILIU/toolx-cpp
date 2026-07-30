# toolx-log 设计记录

> Audience: ToolX 维护者
> Status: Historical design evidence
> Applies to: `toolx-log` 进入 0.3.0 产品链之前的设计阶段
> Source of truth for: 离线诊断边界的历史理由

首个产品用例是 release/deployment triage：读取已经存在的日志，按 severity
汇总，暴露 parse failure，并在 gate 条件满足时稳定失败。

设计明确排除 tail、rotation watching 与实时监控。实时行为会引入 ordering、
lifecycle 和平台 watcher 语义，而首条产品链并不需要。

解析器保留在 `tools/toolx_log.cpp` 内，没有加入 `logsys` 公共 API。只有多个
工具/应用出现相同 ingestion 需求时，才考虑公共 parser。

输入限制为 logsys text 与 JSON-lines。对任意日志做启发式猜测容易产生错误
信心，因此不属于支持范围。

当前公开规范见 [`docs/cli/toolx-log.md`](../../cli/toolx-log.md)。
