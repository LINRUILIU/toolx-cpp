# ToolX Dependencies

> Audience: builders, package consumers, and release maintainers
> Status: Canonical dependency reference
> Applies to: `v0.3.2` release candidate
> Source of truth for: when third-party and system dependencies participate

## Dependency classes

| Class | Dependency | Trigger | Propagation/package effect |
| --- | --- | --- | --- |
| Required | C++20 standard library | Every build | Part of the selected toolchain/runtime ABI |
| Required | CMake 3.20+ | Configure/package workflows | Build-time only |
| Fetched | GoogleTest 1.14.0 | `TOOLX_BUILD_TESTS=ON` | Test targets only; URL and SHA-256 are pinned |
| Optional | OpenSSL | `HTTPX_ENABLE_OPENSSL=ON` | Public link dependency of `httpx`; not in default packages |
| Optional | mbedTLS, mbedx509, mbedcrypto | `HTTPX_ENABLE_MBEDTLS=ON` | Public link dependencies of `httpx`; not in default packages |
| System | `ws2_32` | Windows builds of `sysx`, `httpx`, `logsys` | Public system link edge where declared |
| System | Threads | Test contract helpers | Test-only CMake target |
| Development-only | clang-format | Detected if installed | Adds `format` and `format-check` targets |
| Development-only | clang-tidy | Detected or explicitly enabled | Lint target/compile integration |
| Development-only | gcovr | Coverage build on GCC/Clang | Generates coverage reports |
| Development-only | Ninja | CI and documented examples | Generator choice, not required by ToolX APIs |
| Packaged | ToolX headers, libraries, CMake config and enabled CLIs | Install/CPack | Shipped project artifacts; default packages exclude tests, examples, TLS libraries and offline archives |

## Default build

With both TLS options disabled, ToolX libraries and tools use the C++ standard
library plus platform system libraries. There is no mandatory third-party
runtime package.

Tests are a separate dependency boundary. `FetchContent` downloads the pinned
GoogleTest archive only when tests are enabled. Installing ToolX never installs
GoogleTest.

## TLS backends

### OpenSSL

```bash
cmake -S . -B build/openssl -DHTTPX_ENABLE_OPENSSL=ON
```

CMake uses `find_package(OpenSSL REQUIRED)` and publicly links
`OpenSSL::SSL` and `OpenSSL::Crypto`. Consumers of a static ToolX installation
must therefore make compatible OpenSSL targets available when loading or
linking the exported package.

### mbedTLS

```bash
cmake -S . -B build/mbedtls \
  -DHTTPX_ENABLE_MBEDTLS=ON \
  -DMBEDTLS_ROOT=/path/to/mbedtls/install
```

The build searches for headers and the `mbedtls`, `mbedx509`, and `mbedcrypto`
libraries. All three libraries are required. This path exists in CMake but is
not currently exercised by the public CI matrix.

### Mutual exclusion

Only one backend may be selected. The TLS choice changes `httpx` compilation
and its public link interface, so artifacts built with different backends are
not ABI-interchangeable.

## Offline archives under `.third_party`

The repository tracks `mbedtls-3.6.2.zip` and
`mbedtls-framework-main.zip` as offline source archives. Current ToolX CMake
does **not** extract, configure, or consume these files automatically.

They are therefore:

- not part of the default build;
- not a fallback for `MBEDTLS_ROOT`;
- not installed and therefore absent from binary release archives;
- excluded from CPack source archives by the `.third_party` packaging rule;
- not evidence that the mbedTLS backend is CI verified.

A maintainer may prepare a separate mbedTLS installation from those sources and
point `MBEDTLS_ROOT` at the result, but that preparation is outside the ToolX
build contract.

## Release packaging

Official default binary packages are generated with both TLS backends disabled.
They contain ToolX headers, libraries, CMake exports, and the six selected tools,
but not GoogleTest, OpenSSL, mbedTLS, clang tooling, gcovr, Ninja, or offline
source archives. The separately generated ToolX source archive also excludes
`.third_party`; obtain those optional offline files from the Git repository if needed.
They are not automatic build inputs.

Licensing and redistribution obligations for a custom TLS-enabled package
belong to the producer of that package; the default ToolX archive does not
bundle either TLS implementation.

See [Build and compatibility](build-and-compatibility.md) for supported build
shapes and [httpx](modules/httpx.md) for runtime TLS boundaries.
