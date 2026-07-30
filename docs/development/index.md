# ToolX 开发资料索引

> Audience: ToolX 维护者与贡献者
> Status: Canonical internal-material portal
> Applies to: 当前仓库与保留的历史材料
> Source of truth for: 内部材料的归属、阅读顺序和历史定位

内部材料用于解释“为什么这样做”和“如何维护”，不替代公开兼容性文档。
当内部历史材料与当前源码、测试或公开规范冲突时，以当前实现和公开规范为准，
并修正文档差异。

## 当前维护

- [维护、构建、质量门禁与发布流程](maintaining.md)
- [CLI 产品准入清单](cli-productization.md)
- [根目录开发日志](../../README.dev.md)
- [公开路线图](../roadmap.md)

## 审计材料

- [一级 API 盘点与风险审计](audits/api_inventory.md)
- [一级 API 到产品的断层审计](audits/api_product_gap_audit.md)
- [API 分层覆盖测试矩阵](audits/api_test_matrix.md)

这些材料记录审计口径和风险证据；面向使用者的整理结果已经进入
[`docs/modules`](../modules/) 与 [`docs/cli`](../cli/)。

## 历史设计

- [`toolx-pack` 产品设计](design/toolx-pack-design.md)
- [`toolx-http` 产品设计](design/toolx-http-design.md)
- [`toolx-log` 设计说明](design/toolx-log-design.md)
- [`toolx-inspect` 设计说明](design/toolx-inspect-design.md)

仓库没有独立的 `toolx-config`/`toolx-sync` 历史设计文件，不为目录对称性
反向虚构材料；它们的当前边界以公开 CLI 规范和 Git 历史为准。

## 复盘

- [ToolX 0.3.0 产品化复盘](retrospectives/productization-retro.md)

## 语言与事实源规则

- 公开规范、README、模块/CLI 指南和发布说明使用英文。
- 开发日志、设计、审计与复盘使用中文。
- 公开事实只能在一个规范位置定义；内部材料使用链接引用。
- 历史文档必须标注适用版本，不能用将来时描述已经发布的当前行为。
