# toolx-inspect CLI Reference

> Audience: config/schema operators and terminal users
> Status: Bounded-stable CLI specification
> Applies to: the `0.3.x` line
> Source of truth for: report, deterministic render and bounded run behavior

`toolx-inspect` builds one config/schema inspection model and exposes it as a
scriptable report, deterministic frame, or terminal run. It is not a config
editor, watcher, diff tool, schema authoring assistant, dashboard or general
TUI framework.

## Commands

| Command | Purpose | Business file writes |
| --- | --- | --- |
| `report` | Structured path/value/schema summary | None |
| `render` | Deterministic terminal-style frame | None |
| `run` | Interactive or scripted bounded TUI | None |

All commands accept `--file` or a manifest that supplies it. Manifest values
are defaults; CLI options override them.

## Options

| Area | Options |
| --- | --- |
| Input | `--file`, `--schema`, `--manifest`, `--format auto|json|ini|yaml|toml` |
| Selection | `--path`, `--contains`, `--max-paths`, `--max-issues`, `--focus paths|issues|value` |
| Terminal | `--width`, `--height`, `--no-ansi`, `--script`, `--ticks` |
| Policy/output | `--allow-issues`, `--log-file`, `--json` |

Script tokens are `tab`, `shift-tab`, arrows, `enter`, `esc`, and
`text:VALUE`. A tick count of `-1` is unbounded.

Manifest fields are `file`, `schema`, `format`, `path`, `contains`,
`max_paths`, `max_issues`, `focus`, `width`, `height`, and `allow_issues`.
Unknown fields fail validation.

## Contract

| Exit | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Runtime, load, path or render failure |
| `2` | Usage or parse error |
| `3` | Config, schema or manifest not found |
| `4` | Manifest/schema compilation or schema-issue failure |

Schema issues fail by default; `--allow-issues` converts inspection into a
browsing workflow. JSON uses `schema=toolx.inspect.result`,
`schema_version=1`. Data includes command/input metadata, root/selected kinds,
path and node counts, schema issues, path entries, frame, capabilities and
warnings.

```bash
toolx-inspect report --file app.json --schema schema.json --json
toolx-inspect render --file app.json --schema schema.json \
  --width 100 --height 20
toolx-inspect run --file app.json --script "tab,down,esc" \
  --ticks 4 --no-ansi
```

The [showcase](../examples/product-chain.md) captures the deterministic frame.
Contract coverage lives in
[`cmake/toolx_inspect_cli_contracts.cmake`](../../cmake/toolx_inspect_cli_contracts.cmake).
