# Build and Compatibility

> Audience: builders, package consumers, and release maintainers
> Status: Canonical build guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: supported build shapes and verified compatibility

## Requirements

- CMake 3.20 or newer
- A compiler and standard library with C++20 support
- Git for normal source workflows
- Network access on the first test-enabled configure unless GoogleTest is
  already available in the CMake FetchContent cache

ToolX does not currently claim exact minimum GCC, Clang, Apple Clang, or MSVC
versions. The distinction between a requirement and a verified environment is
intentional.

## Fast development build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The preset builds libraries, tools, examples, and tests while disabling
benchmarks. Enabling tests fetches GoogleTest.

For an offline library/tools/examples build:

```bash
cmake -S . -B build/offline -G Ninja \
  -DTOOLX_BUILD_TESTS=OFF \
  -DTOOLX_BUILD_EXAMPLES=ON \
  -DTOOLX_BUILD_TOOLS=ON \
  -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build/offline --parallel
```

## CMake options

| Option | Direct default | Preset/CI default | Effect |
| --- | --- | --- | --- |
| `TOOLX_BUILD_TESTS` | `ON` | `ON` | Tests and GoogleTest FetchContent |
| `TOOLX_BUILD_EXAMPLES` | `ON` | `ON` | Cookbooks, integrations, showcase and optional benchmarks |
| `TOOLX_BUILD_TOOLS` | `ON` | `ON` | Six installable CLIs |
| `TOOLX_BUILD_BENCHMARKS` | `ON` | `OFF` | `asyncx` and `httpx` benchmarks |
| `TOOLX_ENABLE_CLANG_TIDY` | `OFF` | Clang CI only | Compile-time clang-tidy integration |
| `TOOLX_ENABLE_COVERAGE` | `OFF` | GCC CI only | GCC/Clang coverage flags and report target |
| `HTTPX_ENABLE_OPENSSL` | `OFF` | Dedicated CI job only | OpenSSL TLS backend |
| `HTTPX_ENABLE_MBEDTLS` | `OFF` | `OFF` | mbedTLS backend located under `MBEDTLS_ROOT` or normal search paths |

Deprecated `COPILOT_*` aliases mirror the corresponding `TOOLX_*` options for
one compatibility cycle. New build scripts must use `TOOLX_*` names.

OpenSSL and mbedTLS are mutually exclusive. CMake stops configuration when both
are enabled.

## Verified environments

| Environment | CI coverage | Claim |
| --- | --- | --- |
| Ubuntu latest + GCC | Build, tests, examples, tools, install, package, coverage, release smoke | Verified current CI environment |
| Ubuntu latest + Clang | Build, clang-tidy, tests, install and package | Verified current CI environment |
| Windows latest + MSVC | Ninja build, tests, install and ZIP package | Verified current CI environment |
| macOS latest + Clang | Ninja build, tests, install and TGZ package | Verified current CI environment |
| Ubuntu latest + OpenSSL | Dedicated `httpx_tests` loopback TLS job | Verified optional backend path |
| mbedTLS | CMake discovery and implementation path exist | Supported configuration; not part of the current CI matrix |

“Verified” means the repository workflow exercises the current hosted image. It
does not imply support for every older compiler or operating-system version.

## Windows/MSVC notes

Use a Visual Studio Developer PowerShell or allow a Visual Studio generator to
select the installed toolset. Do not place stale MSVC tool directories into the
global `Path`; CMake may then report a misleading compiler-not-found error.

The repository helper accepts `TOOLX_MSVC_ROOT` for explicitly pinned local
toolsets. The exact Visual Studio installation path is machine-specific and is
not a public compatibility promise.

## Install and consume

```bash
cmake --install build/offline --prefix build/stage
cmake -S examples/install_consumer -B build/consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build/stage"
cmake --build build/consumer --parallel
```

The install tree contains:

```text
bin/                    # when TOOLX_BUILD_TOOLS=ON
include/
lib/
lib/cmake/ToolX/
```

All 13 library targets are exported as `toolx::<name>`. Tools are installed as
executables and are not CMake library targets.

## Compatibility contract

- Documented stable APIs target source compatibility within `0.3.x`.
- ABI compatibility is not promised across compiler, standard-library, runtime
  linkage, TLS backend, architecture, or build-type boundaries.
- The selected TLS backend becomes part of the consumer link interface of
  `toolx::httpx`.
- Default binary release archives contain no TLS backend dependency.
- Package consumers should use the installed CMake config instead of relying on
  build-tree filenames.

See [Dependencies](dependencies.md) for ownership and propagation details and
[the maintainer guide](development/maintaining.md) for release gates.
