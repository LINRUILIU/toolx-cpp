# cfgtool CLI Reference

`cfgtool` is the first productized ToolX CLI. It is the primary `0.1.x`
command-line compatibility contract in this repository.

## Stable Contract

- Exit codes:
  - `0` success or help
  - `1` runtime error
  - `2` usage or parse error
  - `3` not found
  - `4` validation failed
- JSON envelope:

```json
{
  "schema": "cfgtool.result",
  "schema_version": 2,
  "ok": true,
  "code": 0,
  "message": "ok",
  "issues": [],
  "data": {}
}
```

- `schema`, `schema_version`, `ok`, `code`, `message`, `issues`, and `data`
  are additive-only for `0.1.x`.
- Existing fields may gain new siblings, but they should not be removed or
  redefined within the `0.1.x` line.

## Common Options

- `--json`: emit the machine-readable envelope instead of plain-text output
- `--indent <N>`: JSON indentation width for commands that print config data
- `--file <FILE>`: primary config file input for `load`, `doctor`, `get`, `set`, `exists`, `validate`, and snapshot commands
- `--path <PATH>`: config path using dot and `[index]` notation with escaping support

## Commands

| Command | Purpose | Key options |
| --- | --- | --- |
| `load` | Load config and print normalized output | `--file` |
| `adapters` | List parser adapters and active adapter | none |
| `adapter-activate` | Activate a parser adapter for the current process | `--adapter` |
| `doctor` | Inspect config readability, parsing, adapter state, and validation rules | `--file`, validation flags |
| `snapshot-export` | Export the current reloader snapshot to file | `--file`, `--out` |
| `snapshot-restore` | Restore snapshot file and write resolved config | `--file`, `--snapshot`, optional `--out` |
| `get` | Read a node by config path | `--file`, `--path` |
| `set` | Set or create a node by config path | `--file`, `--path`, `--value`, optional `--type` |
| `exists` | Return whether a config path exists | `--file`, `--path` |
| `merge` | Merge base and overlay config files | `--base`, `--overlay`, `--out`, optional `--append-arrays` |
| `validate` | Run validation rules against one config file | `--file`, validation flags |
| `reload-dryrun` | Compare current and candidate config with validation | `--current`, `--candidate`, validation flags |

## Validation Flags

These flags are used by `doctor`, `validate`, and `reload-dryrun`:

- `--require <PATH>`: required path must exist
- `--expect <PATH=TYPE>`: node kind must match `TYPE`
- `--range <PATH=MIN:MAX>`: numeric value must be within range
- `--choice <PATH=V1|V2>`: string or scalar value must match one of the candidates
- `--mutex <PATH1,PATH2[,PATHN]>`: listed paths are mutually exclusive
- `--depends <PATH=DEPENDS_ON>`: `PATH` requires `DEPENDS_ON`
- `--strlen <PATH=MIN:MAX>`: string length range
- `--fail-fast`: stop validation on the first issue

For `set`, `--type` accepts `string`, `int`, `double`, `bool`, `null`, and
`json`.

## Examples

Inspect and edit:

```bash
cfgtool load --file app.json
cfgtool doctor --file app.json --require svc.host --expect svc.port=int --json
cfgtool get --file app.json --path svc.port
cfgtool set --file app.json --path svc.port --value 8080 --type int
cfgtool exists --file app.json --path svc.host
```

`doctor` returns the normal `cfgtool.result` envelope in `--json` mode and adds
diagnostic `data` fields such as `checks`, `recommendations`, `rules_count`,
and `issues_count`. These fields are additive-only inside `0.1.x`.

Snapshot flow:

```bash
cfgtool snapshot-export --file app.json --out snapshot.json --json
cfgtool snapshot-restore --file app.json --snapshot snapshot.json --out restored.json --json
```

Merge and validate:

```bash
cfgtool merge --base base.json --overlay overlay.json --out merged.json --json
cfgtool validate --file merged.json --require svc.host --range svc.port=1:65535 --json
```

Reload dry-run:

```bash
cfgtool reload-dryrun --current current.json --candidate candidate.json --range svc.port=1:65535 --json
```

## Cookbook

Layered config review:

```bash
cfgtool merge --base examples/cfgtool_layered_template/app.base.json \
  --overlay examples/cfgtool_layered_template/app.local.json \
  --out merged.json --json
cfgtool doctor --file merged.json --require svc.host --expect svc.port=int --range svc.port=1:65535
```

Snapshot before experimenting:

```bash
cfgtool snapshot-export --file app.json --out snapshot.json --json
cfgtool set --file app.json --path svc.port --value 9000 --type int
cfgtool snapshot-restore --file app.json --snapshot snapshot.json --out restored.json --json
```

Machine-readable preflight:

```bash
cfgtool doctor --file app.json --require svc.host --expect svc.port=int --json
```

See [`examples/cfgtool_layered_template`](../examples/cfgtool_layered_template)
for a realistic starter layout you can copy into a new project.

## Notes For Maintainers

- Keep help text recognizable enough for black-box tests to catch accidental command removal.
- If a new field is added to the JSON envelope, update contract tests without changing existing field semantics.
- If a new subcommand is ever promoted into the stable contract, update this document, `README.md`, and the `cfgtool_cli_contracts` test together.
