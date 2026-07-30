# sysx

> Audience: C++ consumers requiring a narrow platform abstraction
> Status: Stable support module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: intended `sysx` use and public API grouping

## Role and boundary

`sysx` normalizes compile-time platform/compiler identity, native system and
network errors, clocks, sleeping, synchronization aliases and a movable thread
wrapper. It deliberately remains close to the C++ standard library.

- CMake target: `toolx::sysx`
- Direct/transitive ToolX dependencies: none; Windows publicly links `ws2_32`
- Stability: stable support
- Header: [`include/sysx.h`](../../include/sysx.h)

## Quick start

```cpp
const auto os = sysx::CurrentOs();
const auto deadline = sysx::time::DeadlineAfter(std::chrono::seconds(1));
sysx::thread::Thread worker([] { /* work */ });
worker.Join();
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Platform | `OsKind`, `CompilerKind`, `CurrentOs`, `CurrentCompiler`, `IsWindows/Linux/MacOS` | Compile-time classification |
| Errors | `ErrorDomain`, `ErrorKind`, `Error`, `Status`, `Result<T>` | Carries native code, retryability and message |
| Error capture | `MakeError`, `LastSystemError`, `LastNetworkError`, `IsWouldBlockCode` | Reads platform-native error state |
| Time | steady/system clock aliases, `SteadyNow`, `SystemNow`, `SystemNowMs`, sleep/deadline helpers | Separates monotonic deadlines from wall time |
| Synchronization | mutex and condition-variable aliases | Direct standard-library semantics |
| Threads | `thread::Thread`, `HardwareConcurrency` | Movable, non-copyable `std::thread` facade |

## Platform and ownership behavior

- `LastSystemError` and `LastNetworkError` must be called while the relevant
  native error state still describes the failing operation.
- `Thread` follows `std::thread`: destroying a joinable thread is unsafe, and
  detach transfers lifetime responsibility to the process/application.
- `HardwareConcurrency` may return zero and is an observation, not a resource
  guarantee.
- Clock and wait behavior follows the selected standard library and operating
  system.

## Stability, compatibility, examples, and version changes

The documented platform/error/time/thread surface is stable support. Host
identity and error numbers are observations, not cross-platform constants. No
`sysx`-specific user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/sysx_cookbook.cpp)
- [Basic example](../../examples/sysx_example.cpp)
- [Behavior tests](../../tests/sysx_tests.cpp)
