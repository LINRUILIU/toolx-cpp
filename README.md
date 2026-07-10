# ToolX C++ Toolkit

ToolX is a practical C++20 toolkit for small tools and small-to-medium projects.
`v0.3.2` is a compatible reliability patch: it makes CLI proxy behavior
explicit, upgrades configured `fsx` journals to synchronous write-ahead
recovery records, and adds real OpenSSL loopback TLS CI coverage. The
`v0.3.0` release finalized the first ToolX CLI product chain: config authoring,
config publishing, local packaging, HTTP preflight, log diagnosis, and terminal
inspection.

## Stability

| Module | Status | Purpose |
| --- | --- | --- |
| `argtool` | Stable core | CLI argument parsing, help, constraints, JSON parse output |
| `cfgx` | Stable core | Config parsing, path edits, validation, reload, snapshots |
| `schemax` | Experimental MVP | Config schema compile/validate helpers on top of `cfgx` |
| `asyncx` | Stable core | Thread pool, scheduling, cancellation, task groups, wait helpers |
| `fsx` | Stable core | Atomic writes, batch plans, directory sync, tar archives, watcher basics |
| `logsys` | Stable core | Logging, context fields, trace spans, metrics, async queue |
| `resultx` | Stable core | Cross-module result/status adapters |
| `utils`, `sysx`, `hashx`, `textcodec` | Stable support | Common helpers, platform wrappers, hashes, text codecs |
| `httpx` | Bounded stable | HTTP client, retries, circuit breaker, upload/download; TLS depends on selected backend |
| `tuix` | Experimental foundation | Terminal UI building blocks, styled frames, layouts, and MVP widgets |

Public stability commitments live in [docs/stability.md](docs/stability.md).
`toolx-config`, `toolx-sync`, `toolx-pack`, `toolx-http`, `toolx-log`, and
`toolx-inspect` contract details live in
[docs/toolx-config.md](docs/toolx-config.md), [docs/toolx-sync.md](docs/toolx-sync.md),
[docs/toolx-pack.md](docs/toolx-pack.md), [docs/toolx-http.md](docs/toolx-http.md),
[docs/toolx-log.md](docs/toolx-log.md), and
[docs/toolx-inspect.md](docs/toolx-inspect.md).

## Requirements

- CMake >= 3.20
- C++20 compiler
- Git

CI covers Linux GCC, Linux Clang, Windows MSVC, and macOS Clang.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Equivalent explicit configure:

```bash
cmake -S . -B build-release-v030 -DTOOLX_BUILD_TESTS=ON -DTOOLX_BUILD_EXAMPLES=ON -DTOOLX_BUILD_TOOLS=ON -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build-release-v030 --parallel
ctest --test-dir build-release-v030 --output-on-failure
```

Deprecated `COPILOT_*` CMake options still exist for one compatibility cycle,
but new integrations should use `TOOLX_*`.

## Install And Consume

Install a local stage tree:

```bash
cmake --install build-release-v030 --prefix build-release-v030-stage
```

Consumer project:

```cmake
find_package(ToolX CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE toolx::cfgx toolx::logsys toolx::schemax)
```

Installed tools:

```bash
cmake -S examples/install_consumer -B build-release-v030-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build-release-v030-stage"
cmake --build build-release-v030-consumer --parallel
build-release-v030-stage/bin/toolx-config --help
build-release-v030-stage/bin/toolx-sync --help
build-release-v030-stage/bin/toolx-pack --help
build-release-v030-stage/bin/toolx-http --help
build-release-v030-stage/bin/toolx-log --help
build-release-v030-stage/bin/toolx-inspect --help
```

The install tree is also the shape of the prebuilt release archives:

- `bin/`
- `include/`
- `lib/`
- `lib/cmake/ToolX/`

## Release Artifacts

The `v0.3.2` release is distributed through GitHub Releases with:

- `ToolX-v0.3.2-source.tar.gz`
- `ToolX-v0.3.2-windows-x86_64.zip`
- `ToolX-v0.3.2-linux-x86_64.tar.gz`
- `ToolX-v0.3.2-macos-universal.tar.gz` or `ToolX-v0.3.2-macos-x86_64.tar.gz`
- `SHA256SUMS`

Each binary archive is validated by unpacking it, running `toolx-config --help`,
`toolx-sync --help`, `toolx-pack --help`, `toolx-http --help`, and
`toolx-log --help`, `toolx-inspect --help`, running small pack/log/inspect smoke checks,
and compiling the standalone
[`examples/install_consumer`](examples/install_consumer) project via
`find_package(ToolX)`.

## `toolx-config`

`toolx-config` is the first productized CLI on top of ToolX. It supports config
inspection, editing, merge, validation, reload dry-runs, snapshots, and stable
machine-readable output.

```bash
toolx-config set --file app.json --path svc.port --value 8080 --type int
toolx-config get --file app.json --path svc.port
toolx-config validate --file app.json --schema schema.json --require svc.host --range svc.port=1:65535
toolx-config reload-dryrun --current current.json --candidate candidate.json --json
toolx-config doctor --file app.json --schema schema.json --require svc.host --expect svc.port=int --json
```

`--json` output uses `schema=toolx.config.result` and `schema_version=1`. Fields may
be added, but existing fields are additive-only within the `0.3.x` line. The
full CLI reference is in [docs/toolx-config.md](docs/toolx-config.md).

## `toolx-config` Cookbook

Common workflows are documented in [docs/toolx-config.md](docs/toolx-config.md), including
preflight checks with `toolx-config doctor`, layered merge review, and snapshot
export/restore. A realistic starter lives in
[`examples/toolx_config_layered_template`](examples/toolx_config_layered_template).

## Module Cookbooks

Every public module has a focused `examples/*_cookbook.cpp` executable. Each
cookbook contains 3-5 commented scenarios covering normal use, boundary behavior,
and the current API additions without requiring network access or external
services.

```bash
cmake --build build-release-v030 --target asyncx_cookbook
build-release-v030/asyncx_cookbook
```

## `toolx-sync`

`toolx-sync` is the bounded-stable product CLI for composing and publishing
configuration: base config, local overlays, optional HTTP remote layer, async
execution, validation, dry-run publish reports, atomic write, snapshot, and
audit logging.

```bash
toolx-sync --base app.base.json --overlay app.local.json --out resolved.json \
  --schema schema.json --require svc.port --range svc.port=1:65535 \
  --dry-run --json
toolx-sync --base app.base.json --overlay app.local.json --out resolved.json \
  --snapshot snapshot.json --journal resolved.journal --log-file audit.log --json
```

`toolx-sync` uses its own envelope, `schema=toolx.sync.result` and
`schema_version=1`. It is part of the shipped install set, but its compatibility
promise is narrower than the main `toolx-config` contract. The full CLI reference
is in [docs/toolx-sync.md](docs/toolx-sync.md).

## `toolx-pack`

`toolx-pack` is the bounded-stable product CLI for local staging and packaging.
It stages built files into a release-shaped tree and can create deterministic
tar archives from that tree.

```bash
toolx-pack plan --src build-release-v030-stage --out dist/toolx --archive dist/toolx.tar --json
toolx-pack stage --src build-release-v030-stage --out dist/toolx \
  --include bin --include include --include lib --archive dist/toolx.tar --json
toolx-pack archive --src dist/toolx --archive dist/toolx.tar --json
```

`toolx-pack` uses `schema=toolx.pack.result` and `schema_version=1`. Its MVP
supports deterministic tar only; zip, compression, signing, remote publish, and
dependency discovery are intentionally out of scope. The full CLI reference is
in [docs/toolx-pack.md](docs/toolx-pack.md).

## `toolx-http`

`toolx-http` is the bounded-stable product CLI for runtime endpoint preflight.
It checks HTTP endpoints against status/body expectations and emits stable JSON
for release smoke and deployment gates.

```bash
toolx-http check --url http://127.0.0.1:8080/health \
  --expect-status 200 --expect-body-contains ready --timeout-ms 1000 --json
toolx-http check --manifest http-preflight.json --json
```

`toolx-http` uses `schema=toolx.http.result` and `schema_version=1`. It is not a
general `curl` replacement; load testing, OAuth, download/upload workflows, and
complex body assertion DSLs are intentionally out of scope. The full CLI
reference is in [docs/toolx-http.md](docs/toolx-http.md).

## `toolx-log`

`toolx-log` is the bounded-stable product CLI for offline runtime log diagnosis.
It summarizes logsys text and JSON-lines logs by level, reports parse failures,
filters by simple time/text criteria, and can fail lightweight gates.

```bash
toolx-log summarize --file app.log --min-level warning --json
toolx-log summarize --file app.jsonl --format jsonl \
  --since "2026-06-04 10:00:00.000" --fail-on-level error --json
toolx-log summarize --manifest log-summary.json --json
```

`toolx-log` uses `schema=toolx.log.result` and `schema_version=1`. It is an
offline analyzer, not a real-time tailer, monitoring daemon, alerting system, or
generic arbitrary-log parser. The full CLI reference is in
[docs/toolx-log.md](docs/toolx-log.md).

## `toolx-inspect`

`toolx-inspect` is the bounded-stable product CLI for config/schema terminal
inspection. It can emit a stable report, render a deterministic terminal frame,
or run the same view interactively.

```bash
toolx-inspect report --file app.json --schema schema.json --json
toolx-inspect report --file app.json --path svc.port
toolx-inspect render --file app.json --schema schema.json --width 100 --height 20
toolx-inspect run --file app.json --schema schema.json
```

`toolx-inspect` uses `schema=toolx.inspect.result` and `schema_version=1`.
Schema issues fail by default unless `--allow-issues` is set. It is a bounded
config/schema inspector, not a config editor, live watcher, diff tool, or
general TUI framework. The full CLI reference is in
[docs/toolx-inspect.md](docs/toolx-inspect.md).

## Quality Gates

Release candidates should pass:

```bash
cmake -S . -B build-release-v030 -DTOOLX_BUILD_TESTS=ON -DTOOLX_BUILD_EXAMPLES=ON -DTOOLX_BUILD_TOOLS=ON -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build-release-v030 --target format-check
cmake --build build-release-v030 --parallel
ctest --test-dir build-release-v030 --output-on-failure
cmake --install build-release-v030 --prefix build-release-v030-stage
cmake -S examples/install_consumer -B build-release-v030-consumer -DCMAKE_PREFIX_PATH="$PWD/build-release-v030-stage"
cmake --build build-release-v030-consumer --parallel
cmake -DTOOLX_STAGE_PREFIX=build-release-v030-stage -P cmake/release_smoke.cmake
cpack --config build-release-v030/CPackConfig.cmake
cmake -DPACKAGE_DIR=build-release-v030/packages -P cmake/release_archive_smoke.cmake
cpack --config build-release-v030/CPackSourceConfig.cmake
```

Maintainer workflow details are in [README.dev.md](README.dev.md).
