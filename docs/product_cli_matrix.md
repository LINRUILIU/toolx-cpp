# ToolX 二级产品 CLI 功能盘点

本文固定 ToolX `0.3.x` 六个产品 CLI 的用户承诺、文件副作用、一级 API 依赖和稳定合约。它补充各单独 CLI reference，重点回答跨工具矩阵问题：命令会不会写文件、dry-run/plan 具体保证什么、JSON 输出哪些字段稳定、哪些默认行为可能带来“魔法”。
一级 API 默认值和二级产品承诺之间的桥接审计记录在 [api_product_gap_audit.md](api_product_gap_audit.md)。

## 全局口径

- 产品 CLI contract 独立于一级库 API。库 API 的 `Result`/`Status` 不等于 CLI exit code；CLI exit code 只在本文件和各 CLI reference 中承诺。
- 每个产品 CLI 的 JSON envelope 使用 `schema_version=1`。`schema`、`schema_version`、`ok`、`code`、`message`、`issues`、`data` 以及已测 `data` 字段在 `0.3.x` 内保持 additive-only。
- `--json` 只改变输出格式，不改变业务副作用。
- `manifest` 字段表示 CLI 支持从 JSON manifest 加载默认配置；CLI 显式参数覆盖 manifest 字段。
- dry-run/plan 保证必须按命令理解。没有 dry-run 的 CLI 不应被调用方假设为只读，除非该命令在文件副作用表中明确只读。

## 产品角色

| CLI | 工作流承诺 | 非目标 |
| --- | --- | --- |
| `toolx-config` | 配置文件查看、编辑、合并、校验、快照和 reload dry-run，是主 CLI 兼容性 contract。 | 不是配置服务、secret manager、完整 schema 系统或批量发布工具。 |
| `toolx-sync` | 将 base、remote、overlay 配置合成为 resolved config，并通过原子写发布。 | 不是部署系统、远程配置服务、secret manager 或完整 JSON Schema 实现。 |
| `toolx-pack` | 将构建产物 staged 成发布树，并生成 deterministic tar archive。 | 不是包管理器，不做 zip、压缩、签名、远程发布、依赖发现或 package fingerprint。 |
| `toolx-http` | 对一个或一批 HTTP endpoint 做 runtime preflight gate。 | 不是 `curl` 替代、load tester、OAuth/credential flow、download/upload 工具或复杂 body assertion DSL。 |
| `toolx-log` | 离线读取 logsys text/JSON-lines 日志并生成诊断摘要或 gate 结果。 | 不是实时 tail、监控 daemon、alerting system、metrics exporter 或通用任意日志解析器。 |
| `toolx-inspect` | 对配置和 schema issue 生成 stable report、deterministic render 或 bounded terminal inspection。 | 不是配置编辑器、live watcher、diff tool、schema authoring assistant、dashboard 或通用 TUI framework。 |

## 命令/子命令表

| CLI | 命令 | 必选输入 | 主要输出 | `--json` | manifest | dry-run/plan |
| --- | --- | --- | --- | --- | --- | --- |
| `toolx-config` | `load` | `--file` | normalized config / envelope | 是 | 否 | 否，只读命令 |
| `toolx-config` | `adapters` | 无 | adapter list / active adapter | 是 | 否 | 否，只读命令 |
| `toolx-config` | `adapter-activate` | `--adapter` | active adapter state | 是 | 否 | 否，进程内状态命令 |
| `toolx-config` | `doctor` | `--file` | checks、recommendations、issues | 是 | 否 | 否，只读诊断 |
| `toolx-config` | `snapshot-export` | `--file`、`--out` | snapshot file + envelope | 是 | 否 | 否，会写 `--out` |
| `toolx-config` | `snapshot-restore` | `--file`、`--snapshot` | restored config file / envelope | 是 | 否 | 否，会写 `--out` 或覆盖 `--file` |
| `toolx-config` | `get` | `--file`、`--path` | selected node | 是 | 否 | 否，只读命令 |
| `toolx-config` | `set` | `--file`、`--path`、`--value` | updated file / envelope | 是 | 否 | 否，会覆盖 `--file` |
| `toolx-config` | `exists` | `--file`、`--path` | path existence | 是 | 否 | 否，只读命令 |
| `toolx-config` | `merge` | `--base`、`--overlay`、`--out` | merged config file / envelope | 是 | 否 | 否，会写 `--out` |
| `toolx-config` | `validate` | `--file` + validation flags/schema | validation result | 是 | 否 | 否，只读 gate |
| `toolx-config` | `reload-dryrun` | `--current`、`--candidate` | diff/validation dry-run result | 是 | 否 | 命令本身为 dry-run，不写文件 |
| `toolx-sync` | publish workflow | `--base`、`--out` | resolved config, optional snapshot/journal/log | 是 | 否 | `--dry-run` 不运行 publish plan |
| `toolx-pack` | `stage` | `--src`、`--out` | staged tree, optional archive/journal/log | 是 | 是 | `--dry-run` 只报告 plan |
| `toolx-pack` | `archive` | `--src`、`--archive` | deterministic tar archive | 是 | 是 | `--dry-run` 不写 archive/log |
| `toolx-pack` | `plan` | `--src`、`--out` 或 manifest 等价输入 | planned stage/archive work | 是 | 是 | 等同 `stage --dry-run` |
| `toolx-http` | `check` | `--url` 或 `--manifest` | endpoint check summary | 是 | 是 | 否 |
| `toolx-log` | `summarize` | `--file` repeatable 或 `--manifest` | offline log summary/gate result | 是 | 是 | 否 |
| `toolx-inspect` | `report` | `--file` 或 manifest with file | config/schema report | 是 | 是 | 否，只读输入 |
| `toolx-inspect` | `render` | `--file` 或 manifest with file | deterministic terminal frame | 是 | 是 | 否，只读输入 |
| `toolx-inspect` | `run` | `--file` 或 manifest with file | bounded interactive/scripted TUI run | 是 | 是 | 否，只读输入 |

## 一级 API 依赖

| CLI | 一级 API 依赖 | 依赖说明 |
| --- | --- | --- |
| `toolx-config` | `argtool` + `cfgx` + `schemax` | 参数解析、配置 load/save/path/validation/snapshot、可选 schema validation。 |
| `toolx-sync` | `argtool` + `asyncx` + `cfgx` + `schemax` + `fsx` + `httpx` + `logsys` | 并发加载 base/remote、配置合成和 schema gate、原子 publish、remote fetch、audit log。 |
| `toolx-pack` | `argtool` + `cfgx` + `schemax` + `fsx` + `logsys` | manifest 和 schema validation、目录 diff/sync plan、atomic stage、tar archive、audit log。 |
| `toolx-http` | `argtool` + `cfgx` + `schemax` + `httpx` + `logsys` | manifest/schema、HTTP client preflight、redacted JSON report、audit log。 |
| `toolx-log` | `argtool` + `cfgx` + `schemax` + `logsys` | manifest/schema、logsys level parsing、optional audit log、offline log summary。 |
| `toolx-inspect` | `argtool` + `cfgx` + `schemax` + `tuix` + `logsys` | config/schema inspection model、terminal render/run surface、optional audit log。 |

## 文件副作用

| CLI/命令 | 读文件 | 创建/覆盖文件 | 删除/替换文件 | dry-run/plan 保证 | 输出路径字段存在性 |
| --- | --- | --- | --- | --- | --- |
| `toolx-config load/doctor/get/exists/validate/reload-dryrun` | config、candidate/current、schema | 否 | 否 | `reload-dryrun` 不写文件 | 不承诺新输出路径 |
| `toolx-config set` | `--file` | 覆盖 `--file` | 写入替换原内容 | 无 dry-run | 成功时 `data.file`/plain path 对应文件应存在 |
| `toolx-config merge` | `--base`、`--overlay` | 写 `--out` | 覆盖既有 `--out` | 无 dry-run | 成功时 `--out` 应存在 |
| `toolx-config snapshot-export` | `--file` | 写 `--out` snapshot | 覆盖既有 `--out` | 无 dry-run | 成功时 `--out` 应存在 |
| `toolx-config snapshot-restore` | `--file`、`--snapshot` | 写 `--out`，未提供则覆盖 `--file` | 覆盖目标 config | 无 dry-run | 成功时目标 out/file 应存在 |
| `toolx-sync` | base、overlay、schema；remote URL 显式网络读取 | 写 `--out`，可写 `--snapshot`、`--journal`、`--log-file` | `fsx::Run` atomic write 可替换目标 | `--dry-run` 不运行 `fsx::Run`，不写 `out/snapshot/journal`；当前实现提供 `--log-file` 时会初始化 `FileSink`，可能创建 log file | 非 dry-run 成功时 `out` 必须存在；snapshot/journal/log-file 仅在请求且成功写入时应存在 |
| `toolx-pack stage` | `--src` tree、manifest | 写/覆盖 `--out` stage tree；可写 archive、journal、log-file | `--remove-extra` 可删除 stage 中多余文件 | `--dry-run` 不创建 stage/archive/journal/log | 非 dry-run 成功时 requested stage/archive 应存在 |
| `toolx-pack archive` | staged `--src` tree、manifest | 写 `--archive` tar；可写 log-file | 覆盖既有 archive | `--dry-run` 不写 archive/log | 非 dry-run 成功时 archive 应存在 |
| `toolx-pack plan` | `--src`/manifest | 否 | 否 | 不创建 stage tree、archive、journal、log file | 输出字段是 planned paths，不要求存在 |
| `toolx-http check` | manifest、body-file | 可写 `--log-file` | 否 | 无 dry-run | 无业务输出文件；log-file 仅在请求时可能存在 |
| `toolx-log summarize` | log files、manifest | 可写 `--log-file` | 否 | 无 dry-run | 无业务输出文件；log-file 仅在请求时可能存在 |
| `toolx-inspect report/render/run` | config、schema、manifest | 可写 `--log-file` | 否 | 无 dry-run | 无业务输出文件；frame 在 JSON/plain output 中，不落盘 |

## 稳定合约

| CLI | Exit code | JSON `schema` | 关键 `data` 字段 | Additive-only 规则 |
| --- | --- | --- | --- | --- |
| `toolx-config` | `0` success/help；`1` runtime；`2` usage/parse；`3` not found；`4` validation failed | `toolx.config.result` | command-specific fields；doctor `checks/recommendations/rules_count/issues_count`；schema issues add to `data.schema_issues` | envelope 字段和既有 tested data fields 不删除、不重定义 |
| `toolx-sync` | `0` success/help/dry-run；`1` runtime；`2` usage/parse；`4` validation failed | `toolx.sync.result` | `base`、`overlays`、`remote_url`、`out`、`snapshot`、`journal`、`log_file`、`schema`、`schema_issues`、`append_arrays`、`dry_run`、`steps`、`planned_steps`、`source_trace` | data 可增长；schema issues additively reported |
| `toolx-pack` | `0` success/help/dry-run；`1` runtime；`2` usage/parse；`3` source/stage/manifest/selected path not found；`4` manifest validation failed | `toolx.pack.result` | `command`、`source`、`stage`、`archive`、`manifest`、`dry_run`、`remove_extra`、`entries`、`bytes`、`planned_steps`、`completed_steps`、`archive_format`、`capabilities`、`warnings` | bounded-stable tested fields additive-only |
| `toolx-http` | `0` success/help；`1` transport/runtime；`2` usage/parse；`3` manifest/body-file not found；`4` expectation failure | `toolx.http.result` | `command`、`manifest`、`checked`、`passed`、`failed`、`duration_ms`、`checks`、`warnings`；per-check `name/url/method/ok/status/duration_ms/error_kind/message/expect_status/body_matched` | existing envelope/check fields additive-only |
| `toolx-log` | `0` success/help；`1` runtime/read；`2` usage/parse；`3` file/manifest not found；`4` manifest validation or log gate failure | `toolx.log.result` | `command`、`manifest`、`files`、`file_count`、`format`、`filters`、`lines_read`、`blank_lines`、`parsed`、`matched`、`parse_failures`、`time_missing`、`by_level`、`first_time`、`last_time`、`samples`、`capabilities`、`warnings` | bounded CLI fields additive-only |
| `toolx-inspect` | `0` success/help；`1` runtime/load/render；`2` usage/parse；`3` config/schema/manifest not found；`4` manifest/schema/issue failure | `toolx.inspect.result` | `command`、`file`、`schema_file`、`manifest`、`format`、`root_kind`、`path_count`、`matched_path_count`、`scalar_count`、`object_count`、`array_count`、`selected_path`、`selected_kind`、`selected_value`、`schema_issue_count`、`schema_issues`、`paths`、`frame`、`capabilities`、`warnings` | report/render/run tested fields additive-only |

## 隐式行为清单

| CLI | 隐式行为 |
| --- | --- |
| `toolx-config` | `cfgx` format auto-detect 基于路径扩展名；`set --type` 默认按 value/type 构造 node；`merge` 数组默认覆盖，`--append-arrays` 才追加；`doctor/validate --schema` 的 schema issue 默认 exit `4`；`snapshot-restore` 未提供 `--out` 时覆盖 `--file`。 |
| `toolx-sync` | layer 顺序固定为 base -> remote -> overlays；后层覆盖前层；数组默认覆盖，`--append-arrays` 追加；`--remote-format auto` 基于 remote URL 检测；`--remote-url` 显式触发 HTTP fetch；remote fetch 继承 `httpx` 默认 timeout 和 proxy-from-env；publish 使用 `fsx::ConflictPolicy::Overwrite` + `BestEffort` rollback。 |
| `toolx-pack` | CLI options override manifest；重复 `--include`/`--exclude` 替换 manifest array；无 include 时包含所有 regular files；exclude 在 include 后应用；`plan` 强制 dry-run；archive format 固定 tar；`--remove-extra` 会删除 stage 中不在计划内的路径。 |
| `toolx-http` | 默认 method `GET`；默认 expected status `200:299`；`--url` 与 `--manifest` 互斥；runtime options override manifest defaults；CLI request/expectation options override manifest checks；重复 CLI `--header` 替换 manifest top-level headers；HTTP transport 继承 `httpx` timeout/retry/proxy-from-env 默认；reported URL 默认 redacts sensitive query values。 |
| `toolx-log` | `format=auto` 用首个非空行是否以 `{` 判断 JSONL，否则 logsys text；CLI options override manifest；`--level` 和 `--min-level` override 后互斥；`fail_on_level` 和 `max_parse_errors` 可把成功读取变成 exit `4`。 |
| `toolx-inspect` | `format=auto` 使用 `cfgx` 扩展名检测；manifest 提供默认值，CLI 覆盖；schema issues 默认 exit `4`，`--allow-issues` 放行；`render` 使用 width/height 生成 deterministic frame；`run --script/--ticks` 是 bounded TUI 自动运行。 |

## 魔法风险检查表

| CLI | 隐藏网络访问 | 隐藏写文件 | 隐藏全局状态 | 输出字段和实际文件不一致 | dry-run 写产物 | 审计说明 |
| --- | --- | --- | --- | --- | --- | --- |
| `toolx-config` | 否 | 中 | 中 | 低 | 否 | 写文件只在 `set/merge/snapshot-*`；`adapter-activate` 是进程内 parser adapter 状态；无 manifest/dry-run 混淆。 |
| `toolx-sync` | 低 | 中 | 中 | 中 | 中 | 网络只在显式 `--remote-url`，但 remote fetch 会继承 `httpx` proxy 环境；`cfgx::SetRemoteFetcher` 是全局回调，产品实现用 scoped guard 清理；dry-run 不写 publish plan，但当前 `--log-file` 会初始化 `FileSink`，可能创建文件。 |
| `toolx-pack` | 否 | 高 | 低 | 中 | 低 | `stage`/`archive` 明确写文件；`--remove-extra` 明确删除；`plan`/`--dry-run` 在源码中先返回，不写 stage/archive/journal/log。 |
| `toolx-http` | 低 | 低 | 低 | 低 | 不适用 | endpoint 访问是显式目标，但 `httpx` 默认会读取 proxy 环境；可选 `--log-file` 会写 audit log；无业务输出文件。 |
| `toolx-log` | 否 | 低 | 低 | 低 | 不适用 | 只读 log 输入；可选 `--log-file` 写 audit；gate failure 只影响 exit code/envelope。 |
| `toolx-inspect` | 否 | 低 | 低 | 低 | 不适用 | 只读 config/schema/manifest；可选 `--log-file` 写 audit；`run` 使用 `tuix` 终端状态但不写业务文件。 |

风险等级说明：`低` 表示行为由显式参数触发且范围小；`中` 表示行为容易被误解或受全局/环境状态影响；`高` 表示会复制、覆盖、删除或替换用户文件树。
