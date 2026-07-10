# ToolX Stability Boundary

This document defines the 0.3.x public stability boundary. ToolX does not
promise ABI stability in 0.3.x. It does aim to preserve source compatibility for
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

- Existing public names should remain callable through 0.3.x.
- Existing JSON/CLI fields should remain additive-only through 0.3.x.
- Behavior documented in README and covered by tests should not regress without
  release notes and migration guidance.

`fsx` journals configured through `RunOptions::journal_path` use the `FSXJ3`
write-ahead format in this release. Before each destructive filesystem mutation,
the corresponding idempotent undo entry is written and synchronized to the OS;
successful transactions synchronize `COMMIT` before the normal journal cleanup.
`RecoverFromJournal` remains compatible with `FSXJ1` and `FSXJ2`. FSXJ3
recovery refuses to overwrite a destination that appeared after the journal
entry and preserves the journal and conflicting files for operator review.

This is a recoverable ordering guarantee, not a blanket power-loss atomicity
claim for every filesystem, drive cache, or cross-device rename scenario. Runs
without `journal_path` retain their existing behavior and do not gain a
durability promise.

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

For maintainers, the default package remains TLS-free: do not enable a TLS
backend merely to build a release archive. CI separately configures
`HTTPX_ENABLE_OPENSSL=ON` on Linux and runs only `httpx_tests`; that job creates
its loopback certificate and private key at runtime and writes only the public
certificate used as a temporary CA file.

`httpx` keeps `use_proxy_from_environment=true` by default. `toolx-http` and
the `toolx-sync --remote-url` client expose additive `--no-proxy-from-env`
controls for deterministic callers; absent that explicit opt-out they continue
to honor `HTTP(S)_PROXY` and `NO_PROXY`.

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

The following are intentionally outside the 0.3.x stable surface:

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

This section summarizes the stable CLI contracts. The detailed cross-CLI
command matrix, file side effects, API dependencies, implicit defaults, and
magic-risk checklist live in [product_cli_matrix.md](product_cli_matrix.md).

`toolx-config` was introduced as the first product CLI and remains the primary
0.3.x command-line contract. Stable subcommands currently include
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

`toolx-sync` is a bounded-stable product CLI for config composition and publish.
It supports base config, repeated `--overlay` local layers, optional
`--remote-url`, validation, `--dry-run` publish reports, atomic output,
snapshots, journals, and audit logs. Its output contract starts at
`schema=toolx.sync.result`, `schema_version=1` and may evolve more quickly than
`toolx-config`.

`toolx-sync` exit codes are `0` for success/help/dry-run success, `1` for
runtime errors, `2` for usage or parse errors, and `4` for validation failures.
It accepts `--schema FILE` and reports `schema_issues` additively in JSON mode.

`toolx-pack` is a bounded-stable product CLI for staging release trees and
creating deterministic tar archives. Stable commands are:

```bash
toolx-pack stage --src DIR --out DIR [--archive FILE]
toolx-pack archive --src DIR --archive FILE
toolx-pack plan --src DIR --out DIR [--archive FILE]
```

Its output contract starts at `schema=toolx.pack.result`,
`schema_version=1`. JSON `data` includes `command`, `source`, `stage`,
`archive`, `manifest`, `dry_run`, `remove_extra`, `entries`, `bytes`,
`planned_steps`, `completed_steps`, `archive_format`, `capabilities`, and
`warnings`.

`toolx-pack` exit codes are `0` for success/help/dry-run success, `1` for
runtime errors, `2` for usage or parse errors, `3` for source/stage/manifest
path not found, and `4` for manifest validation failures. Manifests accept
`name`, `version`, `source`, `stage`, `archive`, `include`, `exclude`, and
`remove_extra`; unknown top-level fields are rejected through `schemax`. The MVP
supports deterministic tar only, not zip, compression, signing, remote publish,
or dependency discovery.

`toolx-http` is a bounded-stable product CLI for runtime endpoint preflight.
Stable commands are:

```bash
toolx-http check --url URL [options]
toolx-http check --manifest FILE [options]
```

Its output contract starts at `schema=toolx.http.result`,
`schema_version=1`. JSON `data` includes `command`, `manifest`, `checked`,
`passed`, `failed`, `duration_ms`, `checks`, and `warnings`; each check reports
`name`, `url`, `method`, `ok`, `status`, `duration_ms`, `error_kind`,
`message`, `expect_status`, and `body_matched`.

`toolx-http` exit codes are `0` for success/help, `1` for transport or runtime
errors, `2` for usage or parse errors, `3` for manifest/body-file not found,
and `4` for status/body expectation failures. Manifests accept `checks`,
runtime defaults, and header defaults; unknown top-level and check fields are
rejected through `schemax`. The MVP is endpoint preflight only, not a general
curl replacement, load tester, credential flow, or body assertion DSL.

`toolx-log` is a bounded-stable product CLI for offline runtime log diagnosis.
Stable commands are:

```bash
toolx-log summarize --file FILE [--file FILE...] [options]
toolx-log summarize --manifest FILE [options]
```

Its output contract starts at `schema=toolx.log.result`,
`schema_version=1`. JSON `data` includes `command`, `manifest`, `files`,
`file_count`, `format`, `filters`, `lines_read`, `blank_lines`, `parsed`,
`matched`, `parse_failures`, `time_missing`, `by_level`, `first_time`,
`last_time`, `samples`, `capabilities`, and `warnings`.

`toolx-log` exit codes are `0` for success/help, `1` for runtime or read errors,
`2` for usage or parse errors, `3` for file/manifest not found, and `4` for
manifest validation failures or configured log gate failures. Manifests accept
`files`, `format`, `level`, `min_level`, `contains`, `since`, `until`,
`max_samples`, `fail_on_level`, and `max_parse_errors`; unknown top-level fields
are rejected through `schemax`. The MVP supports offline logsys text and
logsys JSON-lines analysis only, not live tailing, alerting, monitoring, or
generic arbitrary-log parsing.

`toolx-inspect` is a bounded-stable product CLI for config/schema terminal
inspection. Stable commands are:

```bash
toolx-inspect report --file FILE [--schema FILE]
toolx-inspect render --file FILE [--schema FILE]
toolx-inspect run --file FILE [--schema FILE]
```

Its output contract starts at `schema=toolx.inspect.result`,
`schema_version=1`. JSON `data` includes `command`, `file`, `schema_file`,
`manifest`, `format`, `root_kind`, `path_count`, `matched_path_count`,
`scalar_count`, `object_count`, `array_count`, `selected_path`,
`selected_kind`, `selected_value`, `schema_issue_count`, `schema_issues`,
`paths`, `frame`, `capabilities`, and `warnings`.

`toolx-inspect` exit codes are `0` for success/help, `1` for runtime, load, or
render errors, `2` for usage or parse errors, `3` for config/schema/manifest
path not found, and `4` for manifest validation failures, schema compile
failures, or schema validation issues unless `--allow-issues` is set.
Manifests accept `file`, `schema`, `format`, `path`, `contains`, `max_paths`,
`max_issues`, `focus`, `width`, `height`, and `allow_issues`; unknown top-level
fields are rejected through `schemax`. The MVP is config/schema inspection
only, not config editing, live watching, diffing, schema authoring, or a
general TUI framework.

## Versioning

Recommended current public tag: `v0.3.2`.

For 0.3.x:

- Patch releases should be source-compatible for stable modules.
- Minor version increments may add APIs or promote experimental APIs.
- Breaking changes require explicit release notes and migration examples.
