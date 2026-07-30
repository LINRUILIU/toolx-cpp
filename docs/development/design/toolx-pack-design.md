# toolx-pack 产品设计记录

> Audience: ToolX 维护者
> Status: Historical design evidence
> Applies to: `toolx-pack` 进入 0.3.0 产品链之前的设计阶段
> Source of truth for: 历史产品取舍；当前合同以公开 CLI reference 为准

## 产品动机

ToolX-based 小工具需要把已经构建完成的文件整理为稳定 release tree，并生成
可重复的本地归档。`toolx-pack` 只负责 stage 与 deterministic tar，不承担
编译、包管理和远程发布。

## 初始场景

- 复制 binary、config template、docs、license 与静态资源；
- 修改 stage 前先查看 dry-run；
- 重复 stage 时移除 stale 文件；
- 从 stage 生成 deterministic tar；
- 输出 machine-readable 计划/结果；
- 使用 journal 诊断或恢复失败的 stage。

## 组合模块

| 模块 | 设计用途 |
| --- | --- |
| `argtool` | 参数与稳定 usage error |
| `cfgx` | manifest 与 envelope |
| `schemax` | manifest 子集验证 |
| `fsx` | walk、diff、sync plan、journal 与 tar |
| `logsys` | 可选 audit log |

`asyncx`、`hashx`、`resultx` 曾作为后续候选，但没有在 MVP 中形成公共依赖。
设计决定先保留 tools 内私有实现，不提前创建 `packx`。

## 核心取舍

- `stage`、`archive`、`plan` 三个命令；`plan` 等价于 stage dry-run。
- CLI override manifest；未知 manifest 字段拒绝。
- include/exclude 均以 source-relative path 表示。
- archive format 固定 tar。
- zip、compression、signing、remote publish、依赖发现、fingerprint 与
  parallel scan 均延后。

当前公开规范见 [`docs/cli/toolx-pack.md`](../../cli/toolx-pack.md)。
