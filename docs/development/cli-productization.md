# CLI 产品化准入清单

> Audience: ToolX 维护者
> Status: Canonical internal admission policy
> Applies to: 所有候选 CLI
> Source of truth for: 新 CLI 进入公开安装与发布集合的最低条件

## 准入门槛

一个 CLI 只有同时满足以下条件，才能被描述为“已发布产品”：

- 明确的稳定等级、用户工作流与非目标；
- 在 `TOOLX_BUILD_TOOLS` 下存在可安装 binary target；
- 帮助、核心成功/失败路径、exit code 与 JSON 的黑盒合同测试；
- install-tree smoke 与 unpacked archive smoke；
- 独立的公开 CLI reference；
- 至少一个真实用户工作流示例；
- side effects、manifest/CLI precedence、环境变量与隐式默认值进入跨 CLI 矩阵；
- release note、稳定性文档和 CHANGELOG 同步更新。

## 实施模板

1. **产品边界**：先写角色、稳定等级、输入/输出、非目标与失败模型。
2. **构建安装**：接入 tools、install、CPack，不把开发文件混入归档。
3. **合同测试**：覆盖 help、plain/JSON、not-found、usage、业务 gate 与副作用。
4. **发布验证**：更新 install/archive smoke，验证从归档运行。
5. **文档**：新增公开 reference、matrix 条目、showcase 或实际 workflow。

## 默认决策

没有长期兼容承诺需求或没有完成上述门禁时，候选工具留在 examples/内部
脚本，不进入公开安装矩阵。0.3.x 当前不计划新增产品 CLI。
