# ToolX 开发日志

> Audience: ToolX 维护者与贡献者
> Status: Active development log
> Applies to: `v0.3.2` release-candidate cycle
> Source of truth for: 当前开发上下文与阶段性工程记录

本文件只记录开发主线、阶段性结论和进一步阅读入口。稳定的用户说明位于
[`README.md`](README.md) 和 [`docs/index.md`](docs/index.md)；构建、发布、
质量门禁等维护流程已经迁入
[`docs/development/maintaining.md`](docs/development/maintaining.md)。

## 当前周期：v0.3.2 release candidate

最新已标记版本是 `v0.3.1`。当前分支在不改变 `0.3.x` 公共源码兼容边界的
前提下完成三组可靠性加固：

- `toolx-http` 与 `toolx-sync` 可显式关闭环境代理继承；
- `fsx` 配置 journal 时使用同步的 FSXJ3 预写 undo 记录，并在恢复时保护
  journal 之后新出现的目标；
- Linux/OpenSSL CI 使用本机临时证书验证信任链与 hostname mismatch。

当前开发工作转向文档体系收敛：建立唯一事实源、完整模块/CLI 指南、构建依赖
兼容矩阵和可复现的产品链展示。

## 开发里程碑

| 日期 | 里程碑 | 结果 |
| --- | --- | --- |
| 2026-03-31 | 初始工具库与公开/本地文档拆分 | 建立 C++20 模块化基线 |
| 2026-05-31 | `v0.2.0` 能力扩展 | 增加 `schemax`、模块 cookbooks 和侧向 API |
| 2026-06-10 | `v0.3.0` 产品化收口 | 六个 CLI 进入同一安装、合同测试和发布链 |
| 2026-07-10 | `v0.3.1` | 文件系统、下载、日志和发布安全加固 |
| 2026-07-11 | `v0.3.2` candidate | 代理控制、FSXJ3 和真实 OpenSSL TLS 覆盖 |

这些日期来自仓库 Git 历史；功能级变化以 [`CHANGELOG.md`](CHANGELOG.md)
和 [`docs/releases/`](docs/releases/) 为准。

## 当前工程约束

- 0.3.x 只接受兼容修复、既有产品体验改进和文档/合同收敛。
- 不新增 CLI，不扩张公共 API，除非现有契约无法安全使用或测试。
- `schemax` 与 `tuix` 继续保持实验边界。
- 默认发布包不选择 TLS 后端。
- 第三方离线压缩包不等于受支持的自动构建输入；实际参与方式必须以
  CMake 和依赖文档为准。

## 开发资料入口

- [维护、构建与发布流程](docs/development/maintaining.md)
- [开发资料索引](docs/development/index.md)
- [API 与产品审计](docs/development/audits/)
- [历史设计材料](docs/development/design/)
- [产品化复盘](docs/development/retrospectives/)
- [公开路线图](docs/roadmap.md)
- [稳定性事实源](docs/stability.md)
