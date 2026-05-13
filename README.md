# ToolX C++ Toolkit

ToolX is a practical C++20 toolkit for small tools and small-to-medium projects.
It ships as a set of focused libraries plus installable command-line tools.

Current release focus: **ToolX libraries + `cfgtool` CLI**, with `toolx-sync` as
an end-to-end scenario tool for config validation and atomic publishing.

## Modules

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

See [docs/stability.md](docs/stability.md) for the public stability boundary.

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
cmake -S . -B build -DTOOLX_BUILD_TESTS=ON -DTOOLX_BUILD_EXAMPLES=ON -DTOOLX_BUILD_TOOLS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The old `COPILOT_*` CMake options remain as deprecated aliases for one
compatibility cycle. New integrations should use `TOOLX_*`.

## Install And Consume

```bash
cmake --install build --prefix stage
```

Consumer project:

```cmake
find_package(ToolX CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE toolx::cfgx toolx::logsys)
```

Installed tools:

```bash
stage/bin/cfgtool --help
stage/bin/toolx-sync --help
```

## `cfgtool`

`cfgtool` is the first productized CLI on top of ToolX. It supports config
inspection, editing, merge, validation, reload dry-runs, snapshots, and stable
machine-readable output.

```bash
cfgtool set --file app.json --path svc.port --value 8080 --type int
cfgtool get --file app.json --path svc.port
cfgtool validate --file app.json --require svc.host --range svc.port=1:65535
cfgtool reload-dryrun --current current.json --candidate candidate.json --json
```

`--json` output uses `schema=cfgtool.result` and `schema_version=2`. Fields may
be added, but existing fields should not be removed or redefined within the
0.1.x line.

## `toolx-sync`

`toolx-sync` demonstrates a real module composition path: config load, optional
HTTP remote layer, async execution, validation, atomic write, snapshot, and audit
logging.

```bash
toolx-sync --base app.json --out resolved.json --snapshot snapshot.json \
  --require svc.port --range svc.port=1:65535 --json
```

## Quality Gates

Release candidates should pass:

```bash
cmake --build build --target format-check
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix stage
cmake -DTOOLX_STAGE_PREFIX=stage -P cmake/release_smoke.cmake
```

Maintainer workflow details are in [README.dev.md](README.dev.md).
