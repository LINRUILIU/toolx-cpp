# cfgx

> Audience: C++ applications and configuration-tool authors
> Status: Stable core module guide
> Applies to: `v0.3.4` release
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

### Composition source trace

`ComposeLayers` applies base, environment, remote, local and runtime layers in
that order. Its optional `source_trace` describes the resulting scalar leaves
and empty containers, sorted by path. Replacing a subtree removes its old trace
entries; appended array elements use their indices in the final array. Object
keys use the escaping accepted by `ParsePath`. A scalar or empty root uses the
existing `value` marker.

The v0.3.3 path grammar adds `[]` for an empty object key: `[]` accesses a
root object's empty key, `parent.[]` a nested empty key, and `items[0].[]` an
empty key in an array element. A literal key named `[]` remains `\[\]`. This
syntax was previously rejected; earlier valid paths retain their meaning. The
root `value` marker is unchanged and is not a path to an empty object key.

Decimal array indices must fit `size_t`; overflow is rejected during path
parsing instead of wrapping to another element. `SetNode` also rejects indices
that cannot fit the implementation's `Node::Array::max_size()` before changing
the tree. A representable parsed index does not imply an existing element or a
feasible allocation; this is not a general transaction guarantee for allocation
failure.

Attribution records the layer that supplied each surviving value, even when a
later scalar write has the same value. An empty object merged into an object,
or an empty array appended to an array, changes nothing and retains earlier
attribution. Replacing an array, including with an empty array, uses the later
layer. The trace identifies layers, not filenames, history or proof of validation
and publication. A runtime layer is its materialized patch document; removing
an entry from that document does not remove the corresponding base value.

The v0.3.3 JSON parser checks the number grammar and rejects numbers
outside its representable integer/finite-double range, rather than serializing
overflow as `inf`. Numeric range validation rejects non-finite bounds and
values, including `Node` values constructed directly by a caller.

The v0.3.4 maintenance release also decodes JSON `\u` escapes into UTF-8,
including valid surrogate pairs, and rejects unpaired surrogates and literal
U+0000–U+001F in strings. Serialization escapes every control character in that
range, preserving strings and escaped object keys through editing and file saves.

JSON serialization uses the classic locale and enough digits to round-trip
finite doubles exactly. Integral doubles include a decimal point (for example,
`42.0`) so parsing them back retains `NodeKind::Double`; integer nodes remain
integer literals. This changes JSON spelling for some existing double values.
`Node(double)` still accepts programmatically supplied NaN or infinity, and
`ToJson` has no error result for rejecting them. Callers must reject such values
before serialization; this round-trip guarantee covers finite doubles only.

## Stability, compatibility, examples, and version changes

The node/path model, JSON and INI behavior, composition, validation, reload,
snapshots and adapter contracts are stable. Do not infer full YAML, TOML, schema,
encryption-at-rest, or remote-service semantics.

No `cfgx`-specific public API change is recorded for `v0.3.2`; that release's
proxy opt-out is implemented by the `httpx`-backed product workflows.

The v0.3.3 maintenance release corrects existing source attribution for
subtree replacement, array append, empty containers and escaped keys without
changing the public composition API or its merge policy. Trace contents and
double serialization spelling can change, while invalid/unrepresentable numbers
and overflowing/impossible indices are rejected. The product CLI also corrects
its repeated-overlay application order; that can change output in type-changing
sequences. These are documented correctness exceptions, not a file-provenance
product. See the [release compatibility notes](../releases/v0.3.3.md).

- [Annotated cookbook](../../examples/cfgx_cookbook.cpp)
- [Basic example](../../examples/cfgx_example.cpp)
- [Remote bridge](../../examples/cfgx_httpx_remote_example.cpp)
- [Layered CLI template](../../examples/toolx_config_layered_template/README.md)
- [Behavior tests](../../tests/cfgx_tests.cpp)
