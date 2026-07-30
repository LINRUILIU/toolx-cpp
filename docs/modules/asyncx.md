# asyncx

> Audience: C++ applications requiring bounded asynchronous work
> Status: Stable core module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: intended `asyncx` use and public API grouping

## Role and boundary

`asyncx` provides a managed thread pool, delayed and periodic scheduling,
cooperative cancellation, priorities, backpressure, task groups, metrics and
future wait helpers. It does not provide coroutines, distributed execution,
preemptive cancellation, or a general event loop.

- CMake target: `toolx::asyncx`
- Direct dependency: `toolx::sysx`; transitive ToolX dependencies: none
- Stability: stable core
- Header: [`include/asyncx.h`](../../include/asyncx.h)

## Quick start

```cpp
asyncx::ThreadPool pool;
auto submitted = pool.Submit([] { return 42; });
if (!submitted.ok) return 1;
const int value = submitted.value.get();
pool.StopAndJoin(asyncx::StopMode::Drain);
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Lifecycle | `ThreadPool`, `Start`, `Stop`, `Join`, `StopAndJoin`, `StopMode` | Default construction starts a managed pool according to options |
| Submission | `Post`, `TryPost`, `Submit`, deadline/priority/options variants | `Post` is fire-and-forget; `Submit` returns a future |
| Backpressure | `PoolOptions`, `BackpressurePolicy`, queue capacity APIs | Blocking and rejection behavior is explicit |
| Scheduling | `PostDelayed*`, `ScheduleEvery`, `CancelScheduled` | Scheduled IDs are pool-local |
| Cancellation | `CancellationSource`, `CancellationToken`, `TaskOptions` | Cancellation is cooperative; running work is not preempted |
| Grouping | `TaskGroup`, `TaskGroupStats` | Shares a group token and aggregate outcome stats |
| Waiting | `WaitAll`, `WaitAllFor`, `WaitAny`, `WaitAnyFor`, `WaitAnyUntil` | Operates on caller-owned future vectors |
| Observability | `Stats`, `SchedulerStats`, `MetricsSnapshot`, reset/query APIs | Counters describe accepted/executed/scheduled work |
| Errors | `ErrorKind`, `Error`, `Status`, `Result<T>`, `ToString` | Queue/lifecycle/timeout/cancellation errors remain explicit |

## Threading and failure behavior

- The pool owns worker and scheduler threads; callers must stop/join before
  process teardown when deterministic shutdown matters.
- Task exceptions submitted through futures are delivered by `future::get`;
  metrics record task failure where the implementation can observe it.
- Deadlines limit queue/wait operations and do not preempt already running work.
- Cancellation tokens are thread-safe flags but cancellation-aware tasks must
  poll them.
- A `ThreadPool` and `TaskGroup` are non-copyable. Futures remain caller-owned.

## Stability, compatibility, examples, and version changes

Lifecycle, submission, scheduling, cancellation, grouping, wait helpers and
metrics are stable. Exact scheduling order between equal-priority concurrent
tasks is not a portability contract.

No `asyncx`-specific user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/asyncx_cookbook.cpp)
- [Basic example](../../examples/asyncx_example.cpp)
- [Filesystem bridge](../../examples/asyncx_fsx_bridge_example.cpp)
- [HTTP bridge](../../examples/asyncx_httpx_bridge_example.cpp)
- [Logging bridge](../../examples/asyncx_logsys_bridge_example.cpp)
- [Behavior tests](../../tests/asyncx_tests.cpp)
