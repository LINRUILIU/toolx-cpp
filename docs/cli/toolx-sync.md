# toolx-sync CLI Reference

> Audience: configuration publishers and automation users
> Status: Bounded-stable CLI specification
> Applies to: the `0.3.x` line
> Source of truth for: `toolx-sync` composition, publish and output contract

`toolx-sync` composes a required base, an optional remote layer, and zero or
more local overlays, validates the result, and publishes it through `fsx`. It
is not a deployment system, remote configuration service or secret manager.

## Layer and precedence model

1. `--base`
2. optional `--remote-url`
3. repeated `--overlay` values in command order

Later layers win. Arrays replace unless `--append-arrays` is present. An
explicit remote URL is the only condition that triggers network access.

## Contract

| Exit | Meaning |
| --- | --- |
| `0` | Success, help or successful dry-run |
| `1` | Runtime, load, network or publish error |
| `2` | Usage or CLI parse error |
| `4` | cfgx or schemax validation failure |

JSON uses `schema=toolx.sync.result`, `schema_version=1`. Tested data includes
`base`, `overlays`, `remote_url`, `proxy_from_environment`, `out`, `snapshot`,
`journal`, `log_file`, `schema`, `schema_issues`, `append_arrays`, `dry_run`,
`steps`, `planned_steps`, and `source_trace`.

## Options

| Option | Meaning |
| --- | --- |
| `--base FILE`, `-b` | Required base config |
| `--out FILE`, `-o` | Required resolved output |
| `--overlay FILE` | Repeatable local overlay |
| `--remote-url URL` | Optional remote layer |
| `--remote-format auto|json|ini|yaml|toml` | Remote parser format |
| `--no-proxy-from-env` | Disable environment proxy lookup for the remote client |
| `--schema FILE` | Optional schemax schema |
| `--require PATH`, `--range PATH=MIN:MAX` | Repeatable validation rules |
| `--snapshot FILE`, `-s` | Optional resolved snapshot |
| `--journal FILE`, `-j` | Optional fsx journal |
| `--log-file FILE` | Optional audit log |
| `--append-arrays` | Append rather than replace arrays |
| `--dry-run` | Validate/report without running the publish plan |
| `--json`, `--indent N` | Output controls |

Environment proxy inheritance defaults to true. `--no-proxy-from-env` has no
network side effect without `--remote-url`, but still reports
`proxy_from_environment=false`.

## File behavior

Normal success creates/replaces `--out` and requested snapshot/journal/log
paths. Publishing uses overwrite conflict policy and best-effort rollback.
Dry-run does not run the fsx publish plan or write out/snapshot/journal. A
configured `--log-file` may be initialized while preparing logging; consult the
[cross-CLI matrix](matrix.md) when strict no-write behavior is required.

## Examples

```bash
toolx-sync --base app.base.json --overlay app.local.json \
  --out resolved.json --schema schema.json \
  --require svc.port --range svc.port=1:65535 --dry-run --json

toolx-sync --base app.base.json --out resolved.json \
  --snapshot snapshot.json --journal resolved.journal \
  --log-file audit.log --json
```

See the [product-chain showcase](../examples/product-chain.md) and
[`cmake/toolx_sync_cli_scenario.cmake`](../../cmake/toolx_sync_cli_scenario.cmake).
