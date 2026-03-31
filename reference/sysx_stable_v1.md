# sysx Stable V1 Baseline

Date: 2026-03-30

## Scope

Stable V1 covers the following modules:
- platform/compiler detection
- system/network error abstraction
- time abstraction
- sync primitives aliasing
- thread wrapper

It is intentionally scoped to support asyncx internal migration first.
httpx net migration is out of this version scope.

## Public API Contract

Header: include/sysx.h

### Platform/Compiler
- sysx::OsKind
- sysx::CompilerKind
- sysx::CurrentOs()
- sysx::CurrentCompiler()
- sysx::IsWindows()/IsLinux()/IsMacOS()
- sysx::ToString(OsKind/CompilerKind)

### Error
- sysx::ErrorDomain
- sysx::ErrorKind
- sysx::Error
- sysx::Status
- sysx::Result<T>
- sysx::MakeError(domain, native_code, message)
- sysx::OkStatus()
- sysx::MakeErrorStatus(...)
- sysx::LastSystemError(...)
- sysx::LastNetworkError(...)
- sysx::IsWouldBlockCode(...)
- sysx::ToString(ErrorDomain/ErrorKind)

### Time
- sysx::time::SteadyClock/SystemClock aliases
- sysx::time::SteadyNow()/SystemNow()/SystemNowMs()
- sysx::time::SleepFor()/SleepUntil()
- sysx::time::DeadlineAfter()

### Sync
- sysx::sync::Mutex
- sysx::sync::RecursiveMutex
- sysx::sync::ConditionVariable
- sysx::sync::ConditionVariableAny

### Thread
- sysx::thread::Thread
- sysx::thread::HardwareConcurrency()

## Behavioral Guarantees

1. API stability for V1 symbols listed above.
2. Error object always preserves native_code.
3. LastSystemError/LastNetworkError preserve explicit message if provided.
4. Time helpers use steady clock for deadlines by default.
5. Thread/sync wrappers are thin abstractions with std-compatible behavior.

## Integration Status

- asyncx internal wiring migrated to sysx wrappers for thread/sync/time hot paths.
- asyncx external API remains unchanged.

## Validation Status

- Static diagnostics: no errors in edited files.
- Unit test coverage added/updated:
  - tests/sysx_tests.cpp
  - tests/asyncx_tests.cpp (stop/scheduler behavior guards)
- Runtime test execution is currently blocked by CMake Tools project configuration failure in this workspace.

## Next Version Candidates (Out of V1)

1. sysx::net abstraction and httpx socket/DNS migration.
2. thread priority/affinity capability layer.
3. fs/io/memory/process optional modules.
