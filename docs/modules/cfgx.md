# cfgx

> Audience: C++ applications and configuration-tool authors
> Status: Stable core module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: intended `cfgx` use and public API grouping

## Role and boundary

`cfgx` provides a structured node model, path access, format parsing, layered
composition, validation, reload polling, snapshots, adapters and optional
remote-fetch integration. It is not a secret manager, configuration service,
complete YAML/TOML implementation, or full schema language.

- CMake target: `toolx::cfgx`
- Direct/transitive ToolX dependencies: none
- Stability: stable core; YAML/TOML support is a documented practical subset
- Header: [`include/cfgx.h`](../../include/cfgx.h)

## Quick start

```cpp
auto parsed = cfgx::ParseJson(R"({"svc":{"port":8080}})");
if (!parsed.ok) return 1;
cfgx::SetNode(parsed.value, "svc.host", cfgx::Node("127.0.0.1"));
const auto* port = cfgx::GetNode(parsed.value, "svc.port").value;
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Data model | `Node`, `NodeKind`, object/array/scalar accessors | Values own their child data |
| Paths | `ParsePath`, `PathToken`, `GetNode`, `GetNodeMutable`, `SetNode`, `RemoveNode`, `Exists` | Dot/index notation with escaping |
| Parsing/files | `ConfigFormat`, `DetectFormatFromPath`, `ParseJson`, `ToJson`, `LoadFromFile`, `SaveToFile` | JSON/INI stable; YAML/TOML practical subsets |
| Composition | `Merge`, `ComposeLayers`, `ComposeOptions`, `SourceAttribution`, env/runtime layer builders | Later layers override earlier layers unless array append is selected |
| Validation | `ValidationRule`, `ValidationIssue`, `Validate`, built-in rule factories | Validation issues are returned as data |
| Runtime overrides | `RuntimeOverrides` | Builds and applies explicit patch documents |
| Reload/snapshots | `PollReloader`, `ReloadOptions`, `ReloadEvent`, snapshot import/export/restore and audit trail | Polling and callbacks are process-local |
| Extensibility | `ParserAdapter` registry and active-adapter APIs | Registry state is global to the process |
| Remote data | `SetRemoteFetcher`, `LoadFromRemote`, request/response types | Network behavior is supplied by a caller-installed callback |
| Encryption helpers | `SaveEncryptedToFile`, `LoadEncryptedFromFile` | Utility boundary, not a key-management system |

## State, I/O and failure behavior

- `Node` values own their trees; pointer results from `GetNode*` remain valid
  only while the referenced tree is not structurally invalidated.
- Parser-adapter and remote-fetcher registrations are process-global. Tests and
  tools must restore or clear them after scoped use.
- `LoadFromRemote` performs no network work unless a remote fetcher is installed.
- File save, snapshot and encrypted-file APIs can create or replace files.
- Poll reload callbacks run from the reloader's polling activity; callback code
  must avoid unsafe access to shared application state.

## Stability, compatibility, examples, and version changes

The node/path model, JSON and INI behavior, composition, validation, reload,
snapshots and adapter contracts are stable. Do not infer full YAML, TOML, schema,
encryption-at-rest, or remote-service semantics.

No `cfgx`-specific public API change is recorded for `v0.3.2`; the candidate's
proxy opt-out is implemented by the `httpx`-backed product workflows.

- [Annotated cookbook](../../examples/cfgx_cookbook.cpp)
- [Basic example](../../examples/cfgx_example.cpp)
- [Remote bridge](../../examples/cfgx_httpx_remote_example.cpp)
- [Layered CLI template](../../examples/toolx_config_layered_template/README.md)
- [Behavior tests](../../tests/cfgx_tests.cpp)
