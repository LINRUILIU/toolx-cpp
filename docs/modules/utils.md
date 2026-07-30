# utils

> Audience: C++ consumers needing small common helpers
> Status: Stable support module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: intended `utils` use and public API grouping

## Role and boundary

`utils` collects deterministic string, parsing, time, path, error-formatting and
hash helpers that are too small to justify heavier abstractions. It is not a
general portability framework or Unicode normalization library.

- CMake target: `toolx::utils`
- Direct/transitive ToolX dependencies: none
- Stability: stable support
- Header: [`include/utils.h`](../../include/utils.h)

## Quick start

```cpp
const auto parts = utils::str::split("a,b,c", ',');
const auto port = utils::parse::parse_int32("8080");
if (!port.ok) return 1;
```

## Capability and API matrix

| Namespace | Public API | Boundary |
| --- | --- | --- |
| `utils::err` | `join_context`, `format_error` | Text formatting only |
| `utils::str` | trim/split/case/prefix/suffix helpers | ASCII case operations; no locale folding |
| `utils::str` | UTF-8 and GBK byte/codepoint/display-width measurement | Measurement, not validation or normalization |
| `utils::time` | system milliseconds, local timestamp formatting, steady elapsed time | Local formatting follows host timezone |
| `utils::parse` | `parse_int32`, `parse_double`, `parse_bool` | Non-throwing explicit result values |
| `utils::path` | slash normalization, existence, parent creation | `ensure_parent_dir` mutates the filesystem |
| `utils::hash` | one-shot and streaming FNV/CRC32/Adler32 | Non-cryptographic only |
| Result types | `ParseResult<T>`, `Status` | Lightweight module-local results |

## Behavior, stability, compatibility, and version changes

- Helpers are reentrant unless operating on the filesystem or local-time
  facilities.
- Path normalization changes separators, not canonical path identity.
- Display-width helpers implement the documented practical measurements and do
  not replace a complete Unicode terminal-width database.
- Hash functions must not be used for signatures, credentials, integrity
against adversaries, or secret storage.

The documented helper surface is stable support. No `utils`-specific
user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/utils_cookbook.cpp)
- [Basic example](../../examples/utils_example.cpp)
- [Behavior tests](../../tests/utils_tests.cpp)
