# hashx

> Audience: C++ consumers needing small non-cryptographic hashes
> Status: Stable support module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: `hashx` algorithms and safety boundary

## Role and boundary

`hashx` exposes one-shot and streaming FNV-1a 32/64, CRC32, and Adler-32 through
a focused namespace. It delegates implementation primitives to `utils::hash`.
These algorithms are for checks, cache keys, fixtures, and accidental-corruption
detection—not adversarial integrity or password/secret handling.

- CMake target: `toolx::hashx`
- Direct dependency: `toolx::utils`; transitive ToolX dependencies: none
- Stability: stable support
- Header: [`include/hashx.h`](../../include/hashx.h)

## Quick start

```cpp
const std::uint32_t checksum = hashx::crc32("payload");
hashx::Fnv1a64State state;
state.update(chunk.data(), chunk.size());
const std::uint64_t streamed = state.final();
```

## API matrix

| Area | Public API |
| --- | --- |
| Text one-shot | `fnv1a32`, `fnv1a64`, `crc32`, `adler32` |
| Binary one-shot | corresponding `*_bytes(const void*, size_t)` functions |
| Streaming | `Fnv1a32State`, `Fnv1a64State`, `Crc32State`, `Adler32State` with `reset/update/final` |

## Behavior, stability, compatibility, and version changes

All functions are deterministic, allocation-free at the hashing boundary, and
safe to call concurrently when each caller owns its state. State objects are
not internally synchronized.

The module is stable support. No `hashx`-specific user-visible change is
recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/hashx_cookbook.cpp)
- [Basic example](../../examples/hashx_example.cpp)
- [Known-vector tests](../../tests/hashx_tests.cpp)
