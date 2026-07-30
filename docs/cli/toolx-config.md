# toolx-config CLI Reference

> Audience: configuration authors and automation users
> Status: Primary stable CLI specification
> Applies to: the `0.3.x` line
> Source of truth for: `toolx-config` commands, exits, output and side effects

`toolx-config` is the primary ToolX command-line compatibility contract. It
loads, diagnoses, edits, merges, validates and snapshots local configuration.
It is not a configuration service, secret manager or complete schema system.

## Contract

| Exit | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Runtime/load/parse operation failure |
| `2` | Usage or CLI parse error |
| `3` | Requested file, path or adapter not found |
| `4` | Validation or schema failure |

JSON output uses `schema=toolx.config.result`, `schema_version=1`, and the
top-level `ok`, `code`, `message`, `issues`, and `data` fields. Existing tested
fields are additive-only within `0.3.x`.

## Commands and side effects

| Command | Required input | Result | Writes files |
| --- | --- | --- | --- |
| `load` | `--file` | Normalized config | No |
| `adapters` | None | Adapter list and active adapter | No |
| `adapter-activate` | `--adapter` | Process-local adapter state | No |
| `doctor` | `--file` | Checks, recommendations and issues | No |
| `get` | `--file --path` | Selected node | No |
| `exists` | `--file --path` | Path existence | No |
| `set` | `--file --path --value` | Updated config | Replaces `--file` |
| `merge` | `--base --overlay --out` | Merged config | Creates/replaces `--out` |
| `validate` | `--file` plus rules/schema | Gate result | No |
| `reload-dryrun` | `--current --candidate` | Diff/validation report | No |
| `snapshot-export` | `--file --out` | Snapshot | Creates/replaces `--out` |
| `snapshot-restore` | `--file --snapshot` | Restored config | Writes `--out`, or replaces `--file` when omitted |

Common options are `--json`, `--indent N`, `--file FILE`, and `--path PATH`.
Paths use cfgx dot and `[index]` notation.

## Validation

`doctor`, `validate`, and `reload-dryrun` accept repeated rules:

- `--require PATH`
- `--expect PATH=TYPE`
- `--range PATH=MIN:MAX`
- `--choice PATH=V1|V2`
- `--mutex PATH1,PATH2[,PATHN]`
- `--depends PATH=DEPENDS_ON`
- `--strlen PATH=MIN:MAX`
- `--fail-fast`

`doctor` and `validate` also accept `--schema FILE`. Schema issues return exit
`4`, appear in top-level issues, and are added to `data.schema_issues`.

For `set`, `--type` accepts `string`, `int`, `double`, `bool`, `null`, and
`json`. Format auto-detection follows the file extension. Merge arrays replace
by default; `--append-arrays` opts into append behavior.

## Examples

```bash
toolx-config doctor --file app.json --schema schema.json \
  --require svc.host --expect svc.port=int --json

toolx-config set --file app.json --path svc.port --value 8080 --type int

toolx-config merge --base base.json --overlay local.json \
  --out merged.json --json

toolx-config snapshot-export --file app.json --out snapshot.json --json
toolx-config snapshot-restore --file app.json --snapshot snapshot.json \
  --out restored.json --json
```

The copyable [layered template](../../examples/toolx_config_layered_template/README.md)
shows a realistic two-file workflow. Contract coverage lives in
[`cmake/toolx_config_cli_contracts.cmake`](../../cmake/toolx_config_cli_contracts.cmake).
