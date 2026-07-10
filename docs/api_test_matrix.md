# ToolX 一级 API 分层覆盖测试矩阵

本文把 `docs/api_inventory.md` 的一级 API 盘点落成测试覆盖矩阵。矩阵用于维护默认 CTest 中必须稳定运行的 T0/T1/T2 覆盖；T3 平台、TLS 后端、终端和隔离进程行为只在 capability guard、death test 或明确 skip 下覆盖。

## 分层口径

| 层级 | 含义 | 默认要求 |
| --- | --- | --- |
| T0 Surface | public header、核心类型、枚举、最小实例化可编译/链接 | `api_surface_tests` 覆盖所有一级 header |
| T1 Contract | 稳定输入输出、错误码、默认值、序列化格式 | 模块单测覆盖稳定契约 |
| T2 Redline | 静默失败、破坏性副作用、全局状态、环境依赖、回调异常 | 模块单测覆盖高风险边界 |
| T3 Guarded | 平台后端、TLS、终端、death/subprocess、慢/脆弱路径 | guard、death test 或 Deferred 记录 |

## 覆盖矩阵

| 模块 | 主要 API 面 | 风险层级 | 必需测试层 | 已有/新增覆盖 | 目标测试文件 | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| `argtool` | parser builder、converter、constraints、unknown handler、trace/JSON | R1 | T0/T1/T2 | T0 surface；T1 parse/help/JSON；T2 fallback trace、unknown handler trace、converter failure/throw | `tests/api_surface_tests.cpp`, `tests/argtool_tests.cpp` | Covered |
| `cfgx` | node/path、compose/env、reload、adapter、remote、file formats、snapshot/encryption | R1/R2 | T0/T1/T2 | T0 surface；T1 path/merge/format/validation；T2 env isolation、adapter/fetcher reset、remote failure rollback | `tests/api_surface_tests.cpp`, `tests/cfgx_tests.cpp` | Covered |
| `fsx` | batch plan/run/recover、walk/diff/sync、archive/link/watcher | R2/R3 | T0/T1/T2/T3 | T0 surface；T1 batch/walk/archive/link；T2 legacy journal and conflict recovery；T3 子进程 failpoint 验证 FSXJ3 预写后崩溃恢复 | `tests/api_surface_tests.cpp`, `tests/fsx_tests.cpp`, `tests/fsx_journal_failpoint_*.cpp` | Covered |
| `asyncx` | thread pool lifecycle、backpressure、scheduler、task group、cancellation | R3 | T0/T1/T2 | T0 surface；T1 submit/schedule/wait metrics；T2 stop unblock、deadline non-preemption、task exception stats | `tests/api_surface_tests.cpp`, `tests/asyncx_tests.cpp` | Covered |
| `logsys` | logger config、sinks, formatter, profiles, metrics, rolling, fatal policy | R1/R2/R3 | T0/T1/T2/T3 | T0 surface；T1 config/profile/format; T2 bad config, rolling boundary, global logger reconfig; T3 fatal abort death test | `tests/api_surface_tests.cpp`, `tests/logsys_tests.cpp` | Guarded |
| `resultx` | cross-module status/result normalization | R0 | T0/T1 | T0 surface；T1 mapping and formatting contracts | `tests/api_surface_tests.cpp`, `tests/resultx_tests.cpp` | Covered |
| `utils` | string/time/parse/path/hash helpers | R0/R2 | T0/T1/T2 | T0 surface；T1 string/parse/hash/display width；T2 path helper side effect boundaries | `tests/api_surface_tests.cpp`, `tests/utils_tests.cpp` | Covered |
| `sysx` | platform/error/time/thread/sync wrappers | R1/R3 | T0/T1/T2 | T0 surface；T1 error/time; T2 thread/sync timeout and network error classification | `tests/api_surface_tests.cpp`, `tests/sysx_tests.cpp` | Covered |
| `hashx` | non-cryptographic hashes and streaming states | R0 | T0/T1 | T0 surface；T1 known vectors, binary input, streaming parity | `tests/api_surface_tests.cpp`, `tests/hashx_tests.cpp` | Covered |
| `textcodec` | hex/base64/url encode/decode and error taxonomy | R0 | T0/T1 | T0 surface；T1 roundtrip, known vectors, invalid input, buffer capacity/policy boundaries | `tests/api_surface_tests.cpp`, `tests/textcodec_tests.cpp` | Covered |
| `httpx` | client send, retry, redirect, proxy, download/upload, TLS guard | R2/R3 | T0/T1/T2/T3 | T0 surface；T1 local HTTP/redirect/cookie/proxy; T2 env proxy default/disable/no_proxy；T3 Linux/OpenSSL loopback TLS trust and hostname mismatch | `tests/api_surface_tests.cpp`, `tests/httpx_tests.cpp` | Covered in dedicated CI |
| `schemax` | schema compile/validate and cfgx issue conversion | R0 | T0/T1 | T0 surface；T1 unknown keyword, nested paths, additionalProperties, enum scalar typing, compile errors | `tests/api_surface_tests.cpp`, `tests/schemax_tests.cpp` | Covered |
| `tuix` | terminal primitives, input source, frame buffer, MVP widgets/application | R3 | T0/T1/T2/T3 | T0 surface；T1 frame/layout/input; T2 degraded consume modes and byte-oriented TextInput behavior; T3 console backend capability guard | `tests/api_surface_tests.cpp`, `tests/tuix_tests.cpp` | Guarded |

## Deferred / Guarded 项

| 模块 | 项目 | 状态 | 原因 |
| --- | --- | --- | --- |
| `httpx` | 真实外部 HTTPS/TLS 互操作 | Guarded | 默认测试不得依赖外网；Linux/OpenSSL CI 已覆盖本机 loopback trust 与 hostname mismatch，不把第三方站点互操作作为 gate。 |
| `logsys` | `AbortAfterFlush` | Guarded | 使用 GTest death test 隔离进程终止副作用 |
| `tuix` | 真实 Windows console backend | Guarded | 终端能力依赖运行环境；默认测试只验证 capability/fallback，不强制真实交互控制台 |
| `fsx` | 权限拒绝类文件系统错误 | Deferred | Windows/Linux 权限语义不同，默认 CTest 不制造机器相关 ACL 状态 |
