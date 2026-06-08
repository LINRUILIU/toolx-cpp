# `toolx-inspect`

`toolx-inspect` is a bounded-stable CLI for inspecting config files and optional
schemax validation results. It is a hybrid product surface: `report` is stable
for scripted diagnostics, `render` emits a deterministic terminal-style frame,
and `run` opens the same inspection model in a terminal UI.

It does not add a public C++ API. Implementation helpers stay inside
`tools/toolx_inspect.cpp`.

## Commands

```bash
toolx-inspect report --file FILE [--schema FILE] [options]
toolx-inspect render --file FILE [--schema FILE] [options]
toolx-inspect run --file FILE [--schema FILE] [options]
```

All commands also accept `--manifest FILE`. Manifest values provide defaults;
CLI options override manifest fields.

## Options

Input:

- `--file FILE`: config file to inspect.
- `--schema FILE`: optional schemax schema.
- `--manifest FILE`: optional inspect manifest.
- `--format auto|json|ini|yaml|toml`: config format; `auto` uses the cfgx file
  extension detector.

Selection and display:

- `--path PATH`: initial selected cfgx path. Root is `$`.
- `--contains TEXT`: filter displayed paths by path text or scalar preview.
- `--max-paths N`: maximum paths included in output.
- `--max-issues N`: maximum schema issues included in output.
- `--focus paths|issues|value`: initial focused pane for rendered/run views.

Terminal:

- `--width N`, `--height N`: deterministic render size.
- `--no-ansi`: disable ANSI terminal output for `run`.
- `--script TEXT`: comma-separated scripted input tokens.
- `--ticks N`: maximum run ticks; `-1` means unbounded.

Output and audit:

- `--allow-issues`: return success even when schema issues are present.
- `--log-file FILE`: optional audit log.
- `--json`: emit a JSON envelope.

`--script` tokens are `tab`, `shift-tab`, `up`, `down`, `left`, `right`,
`enter`, `esc`, and `text:VALUE`.

## Manifest

Supported top-level fields:

```json
{
  "file": "app.json",
  "schema": "schema.json",
  "format": "auto",
  "path": "svc.port",
  "contains": "svc",
  "max_paths": 50,
  "max_issues": 20,
  "focus": "issues",
  "width": 80,
  "height": 18,
  "allow_issues": false
}
```

Unknown top-level fields are rejected through `schemax`.

## JSON Envelope

JSON output uses:

```json
{
  "schema": "toolx.inspect.result",
  "schema_version": 1,
  "ok": true,
  "code": 0,
  "message": "inspected",
  "issues": [],
  "data": {}
}
```

`data` includes `command`, `file`, `schema_file`, `manifest`, `format`,
`root_kind`, `path_count`, `matched_path_count`, `scalar_count`,
`object_count`, `array_count`, `selected_path`, `selected_kind`,
`selected_value`, `schema_issue_count`, `schema_issues`, `paths`, `frame`,
`capabilities`, and `warnings`.

## Exit Codes

| Code | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Runtime, load, or render error |
| `2` | Usage or parse error |
| `3` | Config, schema, or manifest path not found |
| `4` | Manifest validation failed, schema compile failed, or schema issues found |

Schema issues fail by default. Use `--allow-issues` when the command is for
browsing rather than gating.

## Examples

```bash
toolx-inspect report --file app.json --schema schema.json --json
toolx-inspect report --file app.json --path svc.port
toolx-inspect render --file app.json --schema schema.json --width 100 --height 20
toolx-inspect run --file app.json --schema schema.json
toolx-inspect run --file app.json --script "tab,down,esc" --ticks 4 --no-ansi
toolx-inspect report --manifest inspect.json --allow-issues --json
```

## Non-Goals

The MVP is not a config editor, live file watcher, diff tool, schema authoring
assistant, dashboard, or general TUI framework. `tuix` remains an experimental
foundation; `toolx-inspect` only stabilizes this bounded config/schema
inspection workflow.
