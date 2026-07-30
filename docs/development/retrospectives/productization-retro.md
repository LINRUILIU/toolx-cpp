# ToolX 0.3.0 产品化复盘

> Audience: ToolX 维护者
> Status: Historical retrospective
> Applies to: `v0.3.0` 产品链收口
> Source of truth for: 0.3.0 产品化过程与后续收敛方向

## 版本叙事

`v0.2.0` 证明了 `toolx-config` 这一单工具产品合同；后续 0.2.x 开发逐步加入
其余 bounded-stable CLI。`v0.3.0` 的任务不是继续扩张，而是把六件套整理成
同一安装、测试、发布与文档集合。

## 稳定合同

- stable core 模块在 0.3.x 内保持文档化源码兼容；
- `schemax` 继续 experimental；
- `httpx` 的 HTTPS 继续受 build-time backend 约束；
- `tuix` 保持 experimental foundation；
- `toolx-inspect` 只稳定 bounded config/schema workflow；
- CLI exit code 与 `schema_version=1` JSON envelope 由各产品独立维护。

## 产品闭环

最终链路覆盖：

1. `toolx-config` 编写与审查配置；
2. `toolx-sync` 合成与发布 resolved config；
3. `toolx-pack` stage 与 tar；
4. `toolx-http` runtime preflight；
5. `toolx-log` 离线日志诊断；
6. `toolx-inspect` 配置/schema terminal inspection。

这条链路不是 package manager、deployment platform、secret manager、完整
JSON Schema、监控系统或 TUI framework。

## 技术债务

主要债务是 CLI 内部结构重复：JSON envelope、plain key/value helper、manifest
schema pattern、exit mapping 和线性增长的 release smoke。0.3.0 选择保留局部
重复，因为已有合同测试且尚未出现足够复用压力。未来私有 `clix` helper 必须
在至少再一个版本证明合同稳定后才考虑。

## 风险

- 版本叙事必须区分 0.2.0 单工具基线与 0.3.0 产品链收口。
- 0.3.x JSON 字段保持 additive-only。
- 新 CLI 会削弱当前发布焦点，必须通过准入清单。
- HTTPS backend 选择必须持续出现在 release notes。
- `toolx-inspect` 不能被误读为 `tuix` framework 稳定。

## 后续方向

复盘建议 consolidation over expansion：收紧 release notes、examples 与版本化
文档；跟踪内部 helper 候选；优先改善既有 CLI；使用 0.3.x patch 做可靠性
修复；只有真实用户工作流暴露缺失产品表面后才讨论 0.4.0。

当前方向由 [`docs/roadmap.md`](../../roadmap.md) 接续。
