# `toolx-inspect` Design Notes

`toolx-inspect` turns the existing `tuix_config_inspector` example into a
bounded product CLI without promoting `tuix` to a full application framework.

The first product need is local diagnosis: an operator has a config file, may
have a schema, and needs to quickly see the shape of the document, selected
values, and schema issues. That use case is small enough to stabilize as a CLI
contract and useful enough to complete the current config/publish/package/
preflight/log chain.

The MVP intentionally has two stable surfaces:

- `report` for scripts, gates, and CI checks.
- `render` plus scripted `run` for deterministic terminal inspection tests.

Real interactive `run` behavior is supported, but it remains bounded to this
config/schema view. The CLI does not add retained UI APIs, a widget framework,
or editor behavior. Future improvements can add search, richer panes, or
keyboard shortcuts without changing the JSON envelope or exit-code contract.

The implementation composes existing modules:

- `argtool` for command parsing and help text.
- `cfgx` for config loading, path parsing, node lookup, and JSON output.
- `schemax` for manifest validation and optional config schema validation.
- `tuix` for deterministic frame rendering and terminal application support.
- `logsys` for optional audit logs.

No `inspectx` library is introduced until reuse pressure appears outside this
single CLI.
