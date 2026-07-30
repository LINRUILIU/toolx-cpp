# argtool

> Audience: C++ CLI authors
> Status: Stable core module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: intended `argtool` use and public API grouping

## Role and boundary

`argtool` builds deterministic command-line parsers with typed values, defaults,
repeat handling, relationship constraints, help output, trace diagnostics and a
machine-readable parse result. It is a parser library, not a shell, REPL,
configuration system, or process launcher.

- CMake target: `toolx::argtool`
- Direct/transitive ToolX dependencies: none
- Stability: stable core
- Header: [`include/argtool.h`](../../include/argtool.h)

## Quick start

```cpp
argtool::Parser parser;
parser.SetDescription("example")
    .Option("threads", 'j').Int().Default("4").Range(1, 64).Done()
    .Flag("verbose", 'v').Done();

const auto parsed = parser.Parse(argc, argv);
if (!parsed.ok) return parsed.exit_code;
const int threads = parsed.GetInt("threads");
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Parser definition | `Parser`, `OptionBuilder`, `PositionalBuilder`, `OptionTemplate`, `PositionalTemplate` | Fluent or reusable template definitions |
| Value model | `ValueType`, `ValueCardinality`, `RepeatMode`, `BoolFlagMode`, `RangePolicy` | Controls conversion and repeated inputs |
| Parse output | `ParseResult`, `ParseError`, `ParseErrorKind` | Typed getters use explicit fallbacks |
| Relationships | `MutexGroup`, `DependencyRule`, `ConstraintRule`, `ConstraintContext`, `ConstraintResult` | Built-in and custom constraint pipeline |
| Conversion | `ValueConverter`, `ConvertResult`, `SetGlobalConverter` | Custom converters must return explicit failure data |
| Commands | `SubcommandRouter`, `SubcommandTree`, `SubcommandPath` | Flat dispatch and a two-level descriptive tree |
| Diagnostics | `IParseLogger`, `TraceEvent`, `EnableTrace`, `ResultToJson` | Callbacks are borrowed; parser does not own the logger |
| Help | `HelpLayout`, `HelpText`, program/description/example setters | Help is generated from parser definitions |

## Behavior and ownership

- Parser configuration is mutable; parsing should begin after definitions are
  complete. Concurrent mutation/parsing of the same instance is not promised.
- `IParseLogger*` is non-owning and must outlive parsing.
- Unknown-option handlers and custom converters run synchronously in the caller.
- Configuration errors may be rejected while definitions are built or parsed;
  runtime input errors are represented in `ParseResult`.
- Parsing performs no filesystem or network I/O unless user callbacks do so.

## Stability, compatibility, examples, and version changes

Stable behavior includes typed option/positional parsing, help, constraints,
trace, JSON diagnostics and subcommand metadata. Exact whitespace in help should
not be treated as a serialization format; product CLIs maintain their own
black-box help contracts.

No `argtool`-specific user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/argtool_cookbook.cpp)
- [Basic example](../../examples/argtool_example.cpp)
- [API surface tests](../../tests/api_surface_tests.cpp)
- [Behavior tests](../../tests/argtool_tests.cpp)
