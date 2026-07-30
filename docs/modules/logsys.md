# logsys

> Audience: C++ tools and services requiring structured diagnostics
> Status: Stable core module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: intended `logsys` use and public API grouping

## Role and boundary

`logsys` provides synchronous/asynchronous logging, formatters, sinks, routing,
structured context, trace spans, profiles, rolling files, error dictionaries and
metrics. It is not a log collector, monitoring backend, alerting system, or
arbitrary log-ingestion framework.

- CMake target: `toolx::logsys`
- Direct/transitive ToolX dependencies: none; Windows links `ws2_32`
- Stability: stable core
- Header: [`include/logsys.h`](../../include/logsys.h)

## Quick start

```cpp
auto& logger = logsys::Logger::Instance();
logger.ConfigureSimpleLogger(logsys::LogLevel::Info, true, false);
LOGI("service ready on port %d", 8080);
logger.Flush();
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Events and levels | `LogLevel`, `LogEvent`, `ExtField`, `LogContext` | Events carry origin, code, message and structured fields |
| Logger | `Logger`, default/simple/V2 configuration methods, stream and printf-style logging | Process-global singleton is the primary facade |
| Formatting | `IFormatter`, `TextFormatter`, `JsonFormatter`, render configuration | Text and JSON-lines are the supported product formats |
| Sinks | `ISink`, `ConsoleSink`, `FileSink`, `DebuggerSink`, `UdpSyslogSink` | Each sink owns its destination resources |
| Routing | `LevelRouter`, record/output levels | Separates accepted records from emitted destinations |
| Context and tracing | `ScopedLogContext`, `TraceSpan` | RAII scope inheritance and duration recording |
| Configuration | `LoggerConfigV2`, profile/rolling/schedule/backpressure/remote structs, `ProfileResolverV2` | V2 keeps routing and rendering explicit |
| Error taxonomy | `ErrorCode`, source/module/category/action enums, `ErrorDictionary` | Encodes and formats stable application-oriented metadata |
| Metrics | `LoggerMetricsSnapshot`, query/reset APIs | Operational counters, not an exporter |

## State, threading and failure behavior

- `Logger::Instance()` is process-global. Reconfiguration affects subsequent
  log activity and must be coordinated with application threads.
- Scoped context is inherited according to the implementation's thread-local
  context model; it should not be assumed to migrate automatically with tasks.
- Asynchronous queues may apply configured backpressure or dropping policy.
- `Flush` is the explicit durability/ordering boundary available to callers;
  sink and operating-system buffering still apply.
- `FatalPolicy::AbortAfterFlush` terminates the process and is covered only in
  isolated death tests.
- File, console, debugger and UDP side effects occur only when their sinks are
  configured.

## Stability, compatibility, examples, and version changes

Logger, formatter, sink, context/span, V2 profile and metrics surfaces are
stable. Product log parsing remains private to `toolx-log` until reuse pressure
justifies a public ingestion API.

No `logsys`-specific user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/logsys_cookbook.cpp)
- [Basic example](../../examples/logsys_example.cpp)
- [cfgx-configured example](../../examples/logsys_cfgx_configured_example.cpp)
- [Behavior tests](../../tests/logsys_tests.cpp)
