# ToolX 开发日志

> Audience: ToolX 维护者与贡献者
> Status: Release cycle summary
> Applies to: `v0.3.2` release
> Source of truth for: v0.3.2 周期总结与阶段性工程记录

本文件只记录开发主线、阶段性结论和进一步阅读入口。稳定的用户说明位于
[`README.md`](README.md) 和 [`docs/index.md`](docs/index.md)；构建、发布、
质量门禁等维护流程已经迁入
[`docs/development/maintaining.md`](docs/development/maintaining.md)。

## v0.3.2 周期总结

本轮在不改变 `0.3.x` 公共源码兼容边界的前提下，完成代理控制、文件系统恢复、
HTTP 与 pack 安全加固，以及文档和发布质量门禁收口。

- `toolx-http` 与 `toolx-sync` 可显式关闭环境代理继承；Linux/OpenSSL CI
  使用本机临时证书验证信任链与 hostname mismatch。
- 文件系统树操作拒绝链接后代与越界路径；FSXJ3 通过同步预写日志恢复覆盖、
  类型切换和新建复制父目录，遇到外部路径冲突时保留恢复证据。
- HTTP 在 transport 前验证请求目标、Header 和 multipart 元数据，拒绝歧义
  framing，正确处理相对重定向与 fragment，并限制 multipart 碰撞扫描次数。
- `toolx-pack` 拒绝重叠 source/stage 根目录，先处理多余项及类型冲突再复制，
  保留合法临时文件样式名称与 POSIX 显式选择名称，枚举失败会返回错误。
- clang-tidy、loopback 启动失败、ASan/UBSan 和 cfgx/httpx/fsx fuzz smoke 纳入
  CI；覆盖率 XML/HTML 显式校验，Codecov 使用 OIDC 上传，门槛为行 70%/分支 40%。
- README、CHANGELOG 与 release note 使用统一 Released 合同；tag、版本与正文
  校验不一致时拒绝发布。源码包内容检查排除本地依赖归档、日志与覆盖率中间文件。
- ASan 暴露的调度器 deadline 引用失效已修复。模块/CLI 指南、依赖兼容矩阵与
  可复现产品链展示完成整合。

上述加固由 [PR #4](https://github.com/LINRUILIU/toolx-cpp/pull/4) 收口；复现过程、
逐轮测试和适用边界见 [安全审计记录](docs/development/audits/security-v032.md)。
六 CLI 私有 helper、统一版本和能力自检仍留待后续按实际使用需求评估。

## 开发里程碑

| 日期 | 里程碑 | 结果 |
| --- | --- | --- |
| 2026-03-31 | 初始工具库与公开/本地文档拆分 | 建立 C++20 模块化基线 |
| 2026-05-31 | `v0.2.0` 能力扩展 | 增加 `schemax`、模块 cookbooks 和侧向 API |
| 2026-06-10 | `v0.3.0` 产品化收口 | 六个 CLI 进入同一安装、合同测试和发布链 |
| 2026-07-10 | `v0.3.1` | 文件系统、下载、日志和发布安全加固 |
| 2026-07-11 | `v0.3.2` candidate | 代理控制、FSXJ3 和真实 OpenSSL TLS 覆盖 |
| 2026-09-14 | `v0.3.2` 正式发布 | 完成安全加固、Released 文档冻结与发布门禁收口 |

此前里程碑日期来自仓库 Git 历史；`v0.3.2` 日期对应本次发布文档冻结。功能级变化
以 [`CHANGELOG.md`](CHANGELOG.md) 和 [`docs/releases/`](docs/releases/) 为准。

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
