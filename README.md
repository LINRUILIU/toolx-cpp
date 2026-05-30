# ToolX C++ Toolkit

ToolX is a practical C++20 toolkit for small tools and small-to-medium projects.
The `v0.1.0` release line focuses on installable libraries plus two shipped CLIs:
`cfgtool` as the first productized command-line tool, and `toolx-sync` as the
end-to-end scenario tool for validation and atomic publishing.

## Stability

| Module | Status | Purpose |
| --- | --- | --- |
| `argtool` | Stable core | CLI argument parsing, help, constraints, JSON parse output |
| `cfgx` | Stable core | Config parsing, path edits, validation, reload, snapshots |
| `asyncx` | Stable core | Thread pool, scheduling, priority, wait helpers |
| `fsx` | Stable core | Atomic writes, batch plans, rollback reports, watcher basics |
| `logsys` | Stable core | Logging, rolling files, async queue, structured fields |
| `resultx` | Stable core | Cross-module result/status adapters |
| `utils`, `sysx`, `hashx`, `textcodec` | Stable support | Common helpers, platform wrappers, hashes, text codecs |
| `httpx` | Bounded stable | HTTP client utilities; TLS depends on selected backend |
| `tuix` | Experimental foundation | Terminal UI building blocks, not a committed TUI framework |

Public stability commitments live in [docs/stability.md](docs/stability.md).
`cfgtool` contract details live in [docs/cfgtool.md](docs/cfgtool.md).

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
cmake -S . -B build-release-v010 -DTOOLX_BUILD_TESTS=ON -DTOOLX_BUILD_EXAMPLES=ON -DTOOLX_BUILD_TOOLS=ON -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build-release-v010 --parallel
ctest --test-dir build-release-v010 --output-on-failure
```

Deprecated `COPILOT_*` CMake options still exist for one compatibility cycle,
but new integrations should use `TOOLX_*`.

## Install And Consume

Install a local stage tree:

```bash
cmake --install build-release-v010 --prefix build-release-v010-stage
```

Consumer project:

```cmake
find_package(ToolX CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE toolx::cfgx toolx::logsys)
```

Installed tools:

```bash
cmake -S examples/install_consumer -B build-release-v010-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build-release-v010-stage"
cmake --build build-release-v010-consumer --parallel
build-release-v010-stage/bin/cfgtool --help
build-release-v010-stage/bin/toolx-sync --help
```

The install tree is also the shape of the prebuilt release archives:

- `bin/`
- `include/`
- `lib/`
- `lib/cmake/ToolX/`

## Release Artifacts

The first public release is distributed through GitHub Releases with:

- `ToolX-v0.1.0-source.tar.gz`
- `ToolX-v0.1.0-windows-x86_64.zip`
- `ToolX-v0.1.0-linux-x86_64.tar.gz`
- `ToolX-v0.1.0-macos-universal.tar.gz` or `ToolX-v0.1.0-macos-x86_64.tar.gz`
- `SHA256SUMS`

Each binary archive is validated by unpacking it, running `cfgtool --help` and
`toolx-sync --help`, and compiling the standalone
[`examples/install_consumer`](examples/install_consumer) project via
`find_package(ToolX)`.

## `cfgtool`

`cfgtool` is the first productized CLI on top of ToolX. It supports config
inspection, editing, merge, validation, reload dry-runs, snapshots, and stable
machine-readable output.

```bash
cfgtool set --file app.json --path svc.port --value 8080 --type int
cfgtool get --file app.json --path svc.port
cfgtool validate --file app.json --require svc.host --range svc.port=1:65535
cfgtool reload-dryrun --current current.json --candidate candidate.json --json
cfgtool doctor --file app.json --require svc.host --expect svc.port=int --json
```

`--json` output uses `schema=cfgtool.result` and `schema_version=2`. Fields may
be added, but existing fields are additive-only within the `0.1.x` line. The
full CLI reference is in [docs/cfgtool.md](docs/cfgtool.md).

## `cfgtool` Cookbook

Common workflows are documented in [docs/cfgtool.md](docs/cfgtool.md), including
preflight checks with `cfgtool doctor`, layered merge review, and snapshot
export/restore. A realistic starter lives in
[`examples/cfgtool_layered_template`](examples/cfgtool_layered_template).

## `toolx-sync`

`toolx-sync` demonstrates a real module composition path: config load, optional
HTTP remote layer, async execution, validation, atomic write, snapshot, and
audit logging.

```bash
toolx-sync --base app.json --out resolved.json --snapshot snapshot.json \
  --require svc.port --range svc.port=1:65535 --json
```

`toolx-sync` uses its own envelope, `schema=toolx.sync.result` and
`schema_version=1`. It is part of the shipped install set, but it remains the
scenario CLI rather than the main long-term compatibility contract.

## Quality Gates

Release candidates should pass:

```bash
cmake -S . -B build-release-v010 -DTOOLX_BUILD_TESTS=ON -DTOOLX_BUILD_EXAMPLES=ON -DTOOLX_BUILD_TOOLS=ON -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build-release-v010 --target format-check
cmake --build build-release-v010 --parallel
ctest --test-dir build-release-v010 --output-on-failure
cmake --install build-release-v010 --prefix build-release-v010-stage
cmake -S examples/install_consumer -B build-release-v010-consumer -DCMAKE_PREFIX_PATH="$PWD/build-release-v010-stage"
cmake --build build-release-v010-consumer --parallel
cmake -DTOOLX_STAGE_PREFIX=build-release-v010-stage -P cmake/release_smoke.cmake
cpack --config build-release-v010/CPackConfig.cmake
cmake -DPACKAGE_DIR=build-release-v010/packages -P cmake/release_archive_smoke.cmake
cpack --config build-release-v010/CPackSourceConfig.cmake
```

Maintainer workflow details are in [README.dev.md](README.dev.md).
