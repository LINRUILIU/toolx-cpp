# ToolX Stability Boundary

This document defines the 0.2.x public stability boundary. ToolX does not
promise ABI stability in 0.2.x. It does aim to preserve source compatibility for
the stable core APIs unless a safety or correctness bug requires a breaking
change.

## Stable Core

The following modules are treated as stable enough for normal use in small
tools and small-to-medium C++20 projects:

| Module | Stable surface |
| --- | --- |
| `argtool` | Parser builders, subcommand roots, help layouts, validation constraints, JSON parse-result contract |
| `cfgx` | `Node`, path API, JSON/INI load-save, validation rules, layering, polling reload, snapshots, parser adapter registry |
| `asyncx` | `ThreadPool`, submit/post APIs, wait helpers, scheduling, metrics, priority, cooperative cancellation, `TaskGroup`, backpressure policy |
| `fsx` | `BatchPlan`, `Run`, rollback reports, journal recovery, directory walk/sync, tar archive MVP, polling watcher, link/archive capability reporting |
| `logsys` | Logger configuration, default/simple setup, sinks, structured context fields, trace spans, metrics snapshots, async queue, rolling, JSON config V2, fatal flush policy |
| `resultx` | Result/status normalization helpers across ToolX modules |
| `utils`, `sysx`, `hashx`, `textcodec` | Helper APIs used by the stable modules |

Stable means:

- Existing public names should remain callable through 0.2.x.
- Existing JSON/CLI fields should remain additive-only through 0.2.x.
- Behavior documented in README and covered by tests should not regress without
  release notes and migration guidance.

## Experimental MVP

`schemax` is an experimental MVP layered on top of `cfgx`. It intentionally
supports a practical schema subset first: `type`, `required`, `properties`,
`items`, `minimum`, `maximum`, `enum`, `minLength`, `maxLength`, and
`additionalProperties`.

Stable enough to use:

- `Schema`, `Options`, `Issue`, `Compile`, `Validate`, and `ToCfgxIssues`.
- Schema-backed validation in `toolx-config validate`, `toolx-config doctor`, and
  `toolx-sync` through `--schema`.

Not yet promised:

- Full JSON Schema compliance.
- `$ref`, combinators, formats, pattern validation, or schema draft selection.
- Long-term issue code taxonomy beyond the tested MVP codes.

## Bounded Stable

`httpx` is usable, but its stability is bounded by backend configuration.

Stable in default builds:

- HTTP request/response model.
- Convenience methods for common HTTP verbs.
- Redirect, cookie jar, retry hook/policy, circuit breaker, download/upload
  helpers, proxy option parsing, connection pooling.
- Error classification covered by tests.

Backend-dependent:

- HTTPS requires `HTTPX_ENABLE_OPENSSL` or `HTTPX_ENABLE_MBEDTLS`.
- Only one TLS backend can be enabled at a time.
- Custom CA and verification behavior depends on the selected backend.
- mbedTLS peer verification currently requires a configured CA file.

## Experimental Foundation

`tuix` is a terminal UI foundation, not yet a formal TUI application framework.

Stable enough to use:

- Terminal clear/move/color/print primitives.
- Frame buffer diff rendering.
- Poll-only input abstraction.
- Theme and styled frame cells.
- Basic gap/padding/flex layout controls.
- `Panel`, `TextInput`, and `ListView` MVP widgets used by tests and examples.

Not yet promised:

- Full retained UI tree.
- Advanced widgets.
- Long-term event model.
- Framework-level API compatibility.

`TeeBack` input consume mode remains explicitly experimental and may degrade to
exclusive consume depending on platform/input source.

## Future Work

The following are intentionally outside the 0.2.x stable surface:

- `fsx` zip archive creation. `QueryCapabilities()` reports `tar_archive=true`
  for the deterministic tar MVP and `zip_archive=false`.
- Cryptographic hash guarantees. `hashx`/`utils::hash` are non-cryptographic.
- Full UTF-8/UTF-16/GBK conversion suite in `textcodec`.
- Full YAML/TOML parser compliance. `cfgx` supports practical subsets unless a
  third-party parser adapter is supplied.
- Production-grade authenticated encryption for secrets. `cfgx` encrypted
  persistence is lightweight local protection only.
- ABI compatibility across compiler versions, standard libraries, or build
  configurations.

## CLI Contracts

`toolx-config` is a 0.2.x product contract. Stable subcommands currently include
`load`, `adapters`, `adapter-activate`, `doctor`, `snapshot-export`,
`snapshot-restore`, `get`, `set`, `exists`, `merge`, `validate`, and
`reload-dryrun`.

Exit codes:

| Code | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Runtime error |
| `2` | Usage or parse error |
| `3` | Not found |
| `4` | Validation failed |

JSON envelope:

```json
{
  "schema": "toolx.config.result",
  "schema_version": 1,
  "ok": true,
  "code": 0,
  "message": "ok",
  "issues": [],
  "data": {}
}
```

`toolx-config validate` and `toolx-config doctor` accept `--schema FILE`. Schema
validation failures return exit code `4` and add `schema_issues` in JSON mode
without removing existing fields.

`toolx-sync` is a scenario CLI. Its output contract starts at
`schema=toolx.sync.result`, `schema_version=1` and may evolve more quickly than
`toolx-config`. In `v0.2.0`, it also accepts `--schema FILE` and reports
`schema_issues` additively in JSON mode.

## Versioning

Recommended current public tag: `v0.2.0`.

For 0.2.x:

- Patch releases should be source-compatible for stable modules.
- Minor version increments may add APIs or promote experimental APIs.
- Breaking changes require explicit release notes and migration examples.
