# ToolX Maintainer Guide

This file is for repository maintainers. User-facing setup and examples live in
[README.md](README.md). Stability commitments live in
[docs/stability.md](docs/stability.md).

## Release Shape

The first public release is scoped as:

- ToolX C++ libraries with CMake package export.
- `cfgtool` as the productized CLI entrypoint.
- `toolx-sync` as the scenario tool that proves module composition.
- GitHub release artifacts and smoke-tested install output.

Do not expand the public API surface during release hardening unless the change
is required to make an existing contract testable or usable.

## CMake Options

Use the `TOOLX_*` options in all new scripts and documentation:

| Option | Default | Purpose |
| --- | --- | --- |
| `TOOLX_BUILD_TESTS` | `ON` | Build unit, integration, and CLI contract tests |
| `TOOLX_BUILD_EXAMPLES` | `ON` | Build example binaries |
| `TOOLX_BUILD_TOOLS` | `ON` | Build installable tools: `cfgtool`, `toolx-sync` |
| `TOOLX_BUILD_BENCHMARKS` | `ON` in direct CMake, `OFF` in presets/CI | Build benchmark examples |
| `TOOLX_ENABLE_CLANG_TIDY` | `OFF` | Enable clang-tidy at compile time |
| `TOOLX_ENABLE_COVERAGE` | `OFF` | Enable GCC/Clang coverage instrumentation |

Deprecated `COPILOT_*` aliases remain wired for one compatibility cycle. Do not
add new documentation examples using those names.

## Local Gates

Recommended fast local loop:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release-like local gate:

```bash
cmake -S . -B build-release-check -DTOOLX_BUILD_TESTS=ON -DTOOLX_BUILD_EXAMPLES=ON -DTOOLX_BUILD_TOOLS=ON -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build-release-check --parallel
ctest --test-dir build-release-check --output-on-failure
cmake --install build-release-check --prefix build-release-stage
cmake -DTOOLX_STAGE_PREFIX=build-release-stage -P cmake/release_smoke.cmake
```

Optional quality gates:

```bash
cmake --build build-release-check --target format-check
cmake -S . -B build-tidy -DTOOLX_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy --target lint-check
```

Coverage remains a GCC/Clang-only gate:

```bash
cmake -S . -B build-cov -DTOOLX_BUILD_TESTS=ON -DTOOLX_ENABLE_COVERAGE=ON
cmake --build build-cov --target coverage
```

## CLI Contracts

`cfgtool` is now a release artifact, not just an example binary.

Stable CLI commitments for 0.1.x:

- Exit codes: `0` success, `1` runtime error, `2` usage error, `3` not found,
  `4` validation failed.
- JSON envelope: `schema=cfgtool.result`, `schema_version=2`, `ok`, `code`,
  `message`, `issues`, `data`.
- Existing JSON fields are additive-only within 0.1.x.
- Help text should remain recognizable enough for black-box tests to catch
  accidental command removal.

The `cfgtool_cli_contracts` CTest test runs `cfgtool` as an executable and
covers `load/get/set/exists/merge/validate/reload-dryrun/snapshot/adapters` in
plain and JSON modes.

`toolx-sync` is a scenario tool. Its JSON envelope is intentionally separate:
`schema=toolx.sync.result`, `schema_version=1`.

## Release Checklist

Before tagging `v0.1.0`:

- CI is green on Linux GCC, Linux Clang, Windows MSVC, and macOS Clang.
- `format-check`, build, tests, install, exported package verification, and
  release smoke pass.
- `cfgtool` and `toolx-sync` are present in the installed `bin` directory.
- `docs/stability.md` still matches public headers and README claims.
- `README.md` examples work against an installed package or built tools.
- GitHub release notes clearly say: source-compatible 0.1.x, no ABI guarantee.

## Module Boundaries

Keep module dependencies narrow:

- Core libraries should not depend on tools.
- Tools may compose multiple modules.
- `cfgtool` should remain a thin CLI facade over `cfgx` and `argtool`.
- `toolx-sync` may compose `argtool`, `asyncx`, `cfgx`, `fsx`, `httpx`, and
  `logsys` because that composition is the point of the scenario.
- `tuix` remains a terminal UI foundation until a separate framework decision is
  made after the CLI release is stable.

## HTTP/TLS Matrix

HTTP-only tests are part of the default gate. TLS behavior depends on one
selected backend:

```bash
cmake -S . -B build-ossl -DHTTPX_ENABLE_OPENSSL=ON
cmake -S . -B build-mbedtls -DHTTPX_ENABLE_MBEDTLS=ON -DMBEDTLS_ROOT=/path/to/mbedtls/install
```

`HTTPX_ENABLE_OPENSSL` and `HTTPX_ENABLE_MBEDTLS` are mutually exclusive.
