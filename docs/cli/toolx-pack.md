# toolx-pack CLI Reference

> Audience: release packagers and automation users
> Status: Bounded-stable CLI specification
> Applies to: the `0.3.x` line
> Source of truth for: local staging, tar output and dry-run behavior

`toolx-pack` copies selected regular files into a release-shaped tree and can
create a deterministic tar archive. It is not a package manager and does not
perform builds, dependency discovery, signing, compression or remote publish.

## Commands

| Command | Required input | Writes |
| --- | --- | --- |
| `stage` | `--src --out` or manifest equivalents | Stage tree; optional tar/journal/log |
| `archive` | `--src --archive` | Tar; optional log |
| `plan` | Stage-equivalent inputs | Nothing; equivalent to `stage --dry-run` |

## Contract

| Exit | Meaning |
| --- | --- |
| `0` | Success, help or dry-run success |
| `1` | Runtime failure |
| `2` | Usage or parse error |
| `3` | Source, stage, manifest or selected path not found |
| `4` | Manifest validation failure |

JSON uses `schema=toolx.pack.result`, `schema_version=1`. Tested data includes
`command`, `source`, `stage`, `archive`, `manifest`, `dry_run`, `remove_extra`,
`entries`, `bytes`, `planned_steps`, `completed_steps`, `archive_format`,
`capabilities`, and `warnings`.

## Options, manifest and path rules

Common options are `--manifest`, `--name`, `--version`, repeated `--include`,
repeated `--exclude`, `--remove-extra`, `--dry-run`, `--journal`, `--log-file`,
and `--json`.

Manifest fields are `name`, `version`, `source`, `stage`, `archive`, `include`,
`exclude`, and `remove_extra`. Unknown fields fail schema validation. CLI values
override manifest values; CLI include/exclude lists replace the corresponding
manifest arrays.

Selection paths are source-relative. Absolute, empty, `?`, and `..` segments
are rejected. Without includes, all regular files are selected. Excludes apply
after includes and support exact paths, `subtree/**`, `**/*.suffix`, and segment
wildcards such as `bin/*.dll`.

`--remove-extra` deletes stage paths outside the selected source set. `plan` and
`--dry-run` do not create or modify stage, archive, journal or log outputs.

## Example manifest

```json
{
  "name": "demo",
  "version": "1.2.3",
  "source": "build/install",
  "stage": "dist/demo",
  "archive": "dist/demo.tar",
  "include": ["bin", "README.md", "LICENSE"],
  "exclude": ["**/*.pdb"],
  "remove_extra": true
}
```

```bash
toolx-pack plan --manifest pack.json --json
toolx-pack stage --manifest pack.json --journal pack.journal --json
toolx-pack archive --src dist/demo --archive dist/demo.tar --json
```

Contract coverage lives in
[`cmake/toolx_pack_cli_contracts.cmake`](../../cmake/toolx_pack_cli_contracts.cmake).
