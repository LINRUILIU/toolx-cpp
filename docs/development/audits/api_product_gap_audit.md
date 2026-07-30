# ToolX 一级 API 与二级产品断层审计

> Audience: ToolX 维护者
> Status: Historical audit evidence
> Applies to: `v0.3.2` release-candidate audit baseline
> Source of truth for: 一级 API 到六个 CLI 产品的桥接审计


本文把 `docs/api_inventory.md`、`docs/api_test_matrix.md` 和
`docs/product_cli_matrix.md` 连接起来，专门回答一个问题：一级 C++ API 的默认值、
全局状态和副作用，是否在二级产品 CLI 中被收窄、显式化或测试固定。

## 审计口径

- 只审计当前 `0.3.x` 产品链中的六个 CLI：`toolx-config`、`toolx-sync`、
  `toolx-pack`、`toolx-http`、`toolx-log`、`toolx-inspect`。
- “断层”指一级 API 行为会透出到产品层，但二级 CLI 文档、contract 测试或
  参数边界没有把它讲清楚或钉住。
- 本文不新增 public API 承诺。若需要产品行为变更，应先进入对应 CLI reference
  和 `docs/product_cli_matrix.md`，再补黑盒 contract。

## 结论

当前没有发现“一级能力已经存在但二级产品完全缺失关键工作流”的断层。主要风险
集中在三类继承行为：`httpx` 默认环境代理、`cfgx`/`logsys` 进程级全局状态、
以及 `fsx` overwrite/remove-extra 造成的文件破坏半径。

这三类风险中，`toolx-pack` 的路径边界可以直接用黑盒测试补齐；`toolx-sync` 和
`toolx-http` 的 proxy-from-env 属于一级默认值透出，需要在产品矩阵中显式标注；
`toolx-sync` 的 remote fetcher 生命周期已用内部 scope guard 收口，不再依赖每个
早退路径手动清理。

## API 到产品桥接矩阵

| CLI | 继承的一级 API 风险 | 产品层收窄/缓解 | 剩余断层和状态 |
| --- | --- | --- | --- |
| `toolx-config` | `argtool` 的 parse result 不等于产品 exit code；`cfgx` parser adapter 是进程级状态；`cfgx` 写文件会覆盖目标。 | 产品文档单独承诺 exit code；写文件只在 `set/merge/snapshot-*`；`adapter-activate` 是短生命周期进程内状态命令。 | 已标注。后续若引入持久 adapter registry，需要新增 contract；当前不需要产品修复。 |
| `toolx-sync` | `cfgx::SetRemoteFetcher` 是全局回调；remote fetch 使用 `httpx::Client`，默认读取 proxy 环境变量；`fsx::Run` 使用 overwrite + BestEffort rollback；`logsys::FileSink` 可创建 audit log。 | 网络只在显式 `--remote-url`；remote fetcher 由内部 scope guard 安装和清理；`--no-proxy-from-env` 只关闭远程客户端的环境代理；`--dry-run` 不执行 publish plan，不写 `out/snapshot/journal`；JSON 输出包含 source trace、planned steps、schema issues 和 proxy state。 | 生命周期和 proxy opt-out 已闭合；保留 dry-run log caveat。 |
| `toolx-pack` | `fsx` 默认 overwrite；`BuildSyncPlan(remove_extra)` 可删除 stage 中多余路径；tar 是 MVP；相对路径如果未规范化会扩大选择范围。 | 产品只接受相对 include/exclude；拒绝 parent traversal、absolute path、`?` wildcard；`stage --dry-run`、`archive --dry-run`、`plan` 不写 stage/archive/journal/log。 | 已补黑盒测试：absolute include 和 `?` wildcard exclude 都必须 usage failure，防止 `--remove-extra` 前的选择边界扩大。 |
| `toolx-http` | `httpx` 默认 timeout、retry legacy 字段和 proxy-from-env；网络、TLS backend、proxy 会影响实际 transport。 | endpoint 访问必须由 `--url` 或 manifest 显式给出；根 manifest 可设置 `use_proxy_from_environment`；`--no-proxy-from-env` 以最高优先级关闭环境代理；URL 输出 redacts sensitive query；manifest check 可被 CLI 参数覆盖并有 loopback contract。 | proxy opt-out 已由 CLI/manifest 合同和 loopback test 覆盖。 |
| `toolx-log` | `logsys` parser/level taxonomy 和全局 logger；optional `--log-file` 有文件副作用；日志格式解析是产品子集。 | 产品是 offline-only summary/gate；输入文件显式；gate failure 只影响 exit code/envelope，不写业务输出。 | 基本闭合。保留 parser subset 文档边界，不承诺通用日志解析器。 |
| `toolx-inspect` | `cfgx` format auto-detect/YAML/TOML 子集；`schemax` 是 MVP；`tuix` 是 experimental framework；`logsys` 可写 audit log。 | 产品承诺 stable report/render/run 的 bounded output，不承诺 `tuix` 框架级兼容；schema issue 默认 exit `4`，`--allow-issues` 显式放行。 | 基本闭合。interactive terminal 行为不作为框架 contract，只固定 JSON/frame 字段。 |

## 高风险登记

| ID | 风险 | 影响面 | 当前处理 |
| --- | --- | --- | --- |
| GAP-001 | `httpx::ClientOptions::use_proxy_from_environment=true` 透出到 `toolx-sync --remote-url` 和 `toolx-http check`。 | 同一命令在不同 CI/生产环境可能走代理或被 `NO_PROXY` 改写路径。 | 默认值保持兼容；`toolx-http` 支持 manifest/CLI 关闭，`toolx-sync` 支持远程客户端 CLI 关闭，且 JSON 明示最终状态。 |
| GAP-002 | `cfgx::SetRemoteFetcher` 是全局回调，`toolx-sync` remote fetch 需要严格生命周期。 | 未来新增早退或异常路径时，可能污染同进程后续 cfgx 调用。 | 已补 `ScopedRemoteFetcher` 内部 guard；CLI 进程级黑盒测试无法直接覆盖泄漏，靠代码结构兜底。 |
| GAP-003 | `toolx-sync --dry-run --log-file` 会先初始化 audit log，再进入 dry-run。 | 用户可能把 dry-run 理解成完全零写入。 | 产品 contract 明确 dry-run 无业务写入，只覆盖 `out/snapshot/journal`；log-file caveat 已在矩阵保留。 |
| GAP-004 | `toolx-pack --remove-extra` 在 stage 目标上有删除能力。 | 错误 include/exclude 或路径边界失守时可能删除 staged tree 中的用户文件。 | 已有 dry-run/plan sentinel 测试；新增 absolute include 和 `?` wildcard exclude contract，防止选择边界扩大。 |
| GAP-005 | `toolx-config adapter-activate` 看起来像持久状态，但实际只影响当前 CLI 进程。 | 调用方可能以为会改变后续命令或系统默认 parser。 | 产品矩阵标注为进程内状态命令；不承诺持久 adapter 配置。 |
| GAP-006 | `toolx-inspect run` 使用 experimental `tuix`，但产品 CLI 是 bounded-stable。 | 用户可能误以为 `tuix` 框架 API 也随产品稳定。 | 产品边界明确只承诺 `toolx-inspect` 输出字段和 bounded run，不提升 `tuix` 稳定级别。 |

## 已补合同覆盖

- `toolx-pack stage --include <absolute-path> --json` 必须返回 usage error，并保持标准
  JSON envelope。
- `toolx-pack stage --exclude "tmp/?.tmp" --json` 必须返回 usage error，并保持标准
  JSON envelope。
- 这些用例补在 `cmake/toolx_pack_cli_contracts.cmake`，和已有 parent traversal、
  dry-run/plan sentinel、manifest override 合同一起覆盖 `fsx` 删除/覆盖风险进入产品
  前的路径边界。
