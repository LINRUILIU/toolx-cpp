# toolx-sync CLI Reference

`toolx-sync` is the bounded-stable ToolX CLI for composing configuration layers
and publishing the resolved config atomically. It is a product CLI for config
composition and publish workflows, but its compatibility promise is narrower
than the primary `toolx-config` contract.

## Product Boundary

`toolx-sync` is intended for:

- composing a base config with optional remote and local overlay layers
- validating the resolved config before publish
- producing dry-run publish reports
- publishing the resolved config with `fsx` atomic writes
- writing optional snapshots, journals, and audit logs

It is not a deployment system, secret manager, full JSON Schema implementation,
or remote configuration service.

Layer order is:

1. base config
2. optional remote layer from `--remote-url`
3. zero or more local overlays from repeated `--overlay`

Later layers win for the same path. Multiple overlays are merged in the order
they are supplied. Arrays replace by default; `--append-arrays` appends arrays
while composing.

## Stable Contract

Exit codes:

| Code | Meaning |
| --- | --- |
| `0` | Success, help, or successful dry-run |
| `1` | Runtime error |
| `2` | Usage or parse error |
| `4` | Validation failed |

JSON envelope:

```json
{
  "schema": "toolx.sync.result",
  "schema_version": 1,
  "ok": true,
  "code": 0,
  "message": "published",
  "issues": [],
  "data": {}
}
```

The envelope fields above are additive-only. `data` fields may grow as the
product workflow grows.

Current `data` fields include:

- `base`
- `overlays`
- `remote_url`
- `proxy_from_environment`
- `out`
- `snapshot`
- `journal`
- `log_file`
- `schema`
- `schema_issues`
- `append_arrays`
- `dry_run`
- `steps`
- `planned_steps`
- `source_trace`

## Options

| Option | Purpose |
| --- | --- |
| `--base FILE`, `-b FILE` | Required base config file |
| `--out FILE`, `-o FILE` | Required resolved output file |
| `--overlay FILE` | Local overlay config file; repeatable |
| `--remote-url URL` | Optional remote config layer |
| `--no-proxy-from-env` | Disable `HTTP_PROXY`/`HTTPS_PROXY` and `NO_PROXY` only for `--remote-url` fetches |
| `--remote-format FORMAT` | Remote format: `auto`, `json`, `ini`, `yaml`, or `toml` |
| `--schema FILE` | Optional `schemax` schema |
| `--require PATH` | Validation rule: path must exist; repeatable |
| `--range PATH=MIN:MAX` | Validation rule: numeric bounds; repeatable |
| `--snapshot FILE`, `-s FILE` | Optional resolved snapshot output |
| `--journal FILE`, `-j FILE` | Optional `fsx` journal path |
| `--log-file FILE` | Optional audit log file |
| `--append-arrays` | Append arrays while composing layers |
| `--dry-run` | Validate and print the publish plan without writing files |
| `--json` | Emit the JSON envelope |
| `--indent N`, `-i N` | JSON indentation width for resolved config output |

## Examples

Dry-run a layered publish:

```bash
toolx-sync --base app.base.json --overlay app.local.json --out resolved.json \
  --schema schema.json --require svc.port --range svc.port=1:65535 \
  --dry-run --json
```

Publish with a snapshot and audit log:

```bash
toolx-sync --base app.base.json --overlay app.local.json --out resolved.json \
  --snapshot snapshot.json --journal resolved.journal --log-file audit.log \
  --schema schema.json --json
```

Compose a remote layer before local overlays:

```bash
toolx-sync --base app.base.json --remote-url https://config.example/app.json \
  --overlay app.local.json --out resolved.json --json
```

Remote fetches read the standard proxy environment by default, matching the
existing `httpx` behavior. Add `--no-proxy-from-env` to bypass that environment
for the remote client. The flag is accepted without `--remote-url`, reports
`data.proxy_from_environment=false`, and causes no network activity by itself.

## Maintainer Notes

- Keep `toolx-sync` install and archive smoke coverage aligned with
  `cmake/release_smoke.cmake` and `cmake/release_archive_smoke.cmake`.
- Extend `cmake/toolx_sync_cli_scenario.cmake` whenever adding a public option
  or JSON field that operators are expected to consume.
- Keep the loopback remote-proxy contract aligned with the default-on behavior
  and the `--no-proxy-from-env` bypass.
- Prefer additive `data` fields over envelope changes.
