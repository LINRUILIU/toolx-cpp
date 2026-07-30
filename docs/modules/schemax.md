# schemax

> Audience: C++ applications needing a small cfgx schema subset
> Status: Experimental MVP module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: supported schema subset and validation behavior

## Role and boundary

`schemax` compiles a deliberately small schema document and validates
`cfgx::Node` trees. It rejects unknown keywords instead of silently accepting a
larger standard. It is not a complete JSON Schema implementation and does not
promise compatibility with a particular JSON Schema draft.

- CMake target: `toolx::schemax`
- Direct dependency: `toolx::cfgx`; transitive ToolX dependencies: none
- Stability: experimental MVP
- Header: [`include/schemax.h`](../../include/schemax.h)

## Quick start

```cpp
auto schema_node = cfgx::ParseJson(R"({"type":"object","required":["svc"]})");
auto schema = schemax::Compile(schema_node.value);
if (!schema.ok) return 1;
const auto issues = schemax::Validate(document, schema.value);
```

## API and supported-keyword matrix

| Area | Public API or keyword | Behavior |
| --- | --- | --- |
| Compilation | `Schema`, `Compile` | Validates keyword names and values, fail-fast on malformed schema |
| Validation | `Validate`, `Options::fail_fast` | Empty issue vector means success |
| Issues | `Issue { path, code, message }` | Root path is `$`; nested paths use cfgx dot/index notation |
| Integration | `ToCfgxIssues` | Converts into `cfgx::ValidationIssue` |
| Structure | `type`, `required`, `properties`, `items`, `additionalProperties` | Object/array subset |
| Values | `minimum`, `maximum`, `enum`, `minLength`, `maxLength` | Scalar constraints in the tested MVP |

Unknown keywords and malformed keyword values make `Compile` fail. Document
validation problems are returned as `Issue` data and are not transport errors.
Deterministic visit order supports fail-fast editor/CLI use, but callers should
not treat message prose as a machine code; use `Issue::code` and `path`.

## Stability, compatibility, examples, and version changes

The module is useful and contract-tested but remains experimental. The keyword
subset may grow additively; promoting it to stable requires a separate scope and
compatibility decision.

No `schemax`-specific user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/schemax_cookbook.cpp)
- [Behavior tests](../../tests/schemax_tests.cpp)
- [cfgx module guide](cfgx.md)
