# resultx

> Audience: integrators composing multiple ToolX modules
> Status: Stable support module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: cross-module result normalization

## Role and boundary

`resultx` is a header-only adapter layer that maps existing `cfgx`, `fsx`,
`asyncx`, `httpx`, and `sysx` results into the `sysx` error shape. It does not
replace module-native errors inside each module or define CLI exit codes.

- CMake target: `toolx::resultx` (interface library)
- Declared direct dependency: `toolx::sysx`; transitive ToolX dependencies: none
- Header adapters also reference `asyncx`, `cfgx`, `fsx`, and `httpx`
- Stability: stable support
- Header: [`include/resultx.h`](../../include/resultx.h)

## Quick start

```cpp
cfgx::Status cfg_status{false, "invalid config"};
resultx::Status status = resultx::FromCfgx(cfg_status);
if (!status.ok) std::cerr << resultx::FormatError(status.error);
```

## API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Shared aliases | `ErrorDomain`, `ErrorKind`, `Error`, `Status`, `Result<T>` | Alias the `sysx` types |
| Construction | `MakeError`, `OkStatus` | Builds normalized values |
| Kind mapping | `MapAsyncxErrorKind`, `MapHttpxErrorKind` | Deliberately lossy common taxonomy |
| Adapters | `FromSysx`, `FromCfgx`, `FromFsx`, `FromAsyncx`, `FromHttpx` | Preserve message and available retry/native/status metadata |
| Propagation | `Propagate<T>` | Converts normalized status to a typed result |
| Rendering | `FormatError` | Stable human-readable domain/kind summary, not JSON |

## Link and error behavior

- Because all adapters are inline, link every underlying module whose functions
  are used. The cookbook links `resultx`, `asyncx`, `cfgx`, `fsx`, and `httpx`.
- Mapping into a common taxonomy loses module-specific distinctions; retain the
  original error where exact product decisions require it.
- HTTP errors map into the network domain and reuse `http_status` as the native
  code field; callers should not confuse that with an OS error number.
- No adapter performs I/O or owns the input value.

## Stability, compatibility, examples, and version changes

The adapter aliases and conversions are stable support, while the underlying
module remains authoritative for its native error detail. No `resultx`-specific
user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/resultx_cookbook.cpp)
- [Behavior tests](../../tests/resultx_tests.cpp)
