# toolx-inspect 设计记录

> Audience: ToolX 维护者
> Status: Historical design evidence
> Applies to: `toolx-inspect` 进入 0.3.0 产品链之前的设计阶段
> Source of truth for: bounded terminal inspection 的历史理由

`toolx-inspect` 将 `tuix_config_inspector` example 产品化，但不把 `tuix`
升级为通用 application framework。

首个需求是本地诊断：操作人员已有 config，可选 schema，希望快速看到文档
结构、选中值与 schema issue。这一需求足够小，可以建立稳定 CLI contract。

设计阶段确定两类稳定表面：

- `report`：脚本、gate 与 CI；
- `render` 和 scripted `run`：可重复的 terminal inspection。

真实交互 `run` 受支持，但仅限 config/schema view。编辑器、live watcher、
通用 widget framework 均不在产品边界。当前公开规范见
[`docs/cli/toolx-inspect.md`](../../cli/toolx-inspect.md)。
