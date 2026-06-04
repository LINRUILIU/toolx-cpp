# `toolx-pack` CLI Reference

`toolx-pack` stages a built directory into a release-shaped tree and can create
a deterministic tar archive from that tree. It is a bounded-stable product CLI:
the command surface, exit codes, JSON envelope, and tested fields are stable,
while archive formats beyond tar and richer package metadata remain out of
scope for the MVP.

`toolx-pack` does not add a public C++ library API. Implementation helpers stay
inside `tools/toolx_pack.cpp` until a reusable package API has a concrete need.

## Commands

```bash
toolx-pack stage --src DIR --out DIR [--archive FILE]
toolx-pack archive --src DIR --archive FILE
toolx-pack plan --src DIR --out DIR [--archive FILE]
```

- `stage` copies selected regular files from `--src` into `--out`.
- `archive` creates a deterministic tar archive from an existing staged tree.
- `plan` is equivalent to `stage --dry-run`: it reports planned work and does
  not create the stage tree, archive, journal, or log file.

## Options

Common options:

| Option | Meaning |
| --- | --- |
| `--manifest FILE` | Load pack config from a JSON manifest. |
| `--name TEXT` | Package name metadata. |
| `--version TEXT` | Package version metadata. |
| `--include PATH` | Include a source-relative file or directory. Repeatable. |
| `--exclude PATTERN` | Exclude a source-relative pattern. Repeatable. |
| `--remove-extra` | Remove staged files that are not in the selected source set. |
| `--dry-run` | Report the plan without writing files. |
| `--journal FILE` | Write the `fsx::Run` journal for non-dry-run staging. |
| `--log-file FILE` | Write an audit log for non-dry-run commands. |
| `--json` | Emit the stable JSON envelope. |

CLI options override manifest fields. Repeated `--include` or `--exclude`
replace the manifest array for that field.

## Manifest

Supported top-level fields:

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

Unknown top-level fields are rejected through `schemax` and return exit code
`4`.

## Path Rules

`include` paths and `exclude` patterns are always relative to `--src`.
Absolute paths, empty paths, `?`, and any path segment equal to `..` are
rejected. Internal reporting normalizes separators to `/`.

If no include path is provided, `stage` includes all regular files under
`--src`. Excludes are applied after includes.

Supported exclude patterns:

- exact relative path: `README.md`
- subtree: `debug/**`
- suffix: `**/*.pdb`
- segment wildcard: `bin/*.dll`

## JSON Output

`--json` always uses this envelope:

```json
{
  "schema": "toolx.pack.result",
  "schema_version": 1,
  "ok": true,
  "code": 0,
  "message": "staged",
  "issues": [],
  "data": {}
}
```

`data` contains at least:

- `command`, `source`, `stage`, `archive`, `manifest`
- `dry_run`, `remove_extra`
- `entries`, `bytes`
- `planned_steps`, `completed_steps`
- `archive_format`
- `capabilities`
- `warnings`

Fields may be added in later releases, but existing tested fields are
additive-only within this bounded-stable contract.

## Exit Codes

| Code | Meaning |
| --- | --- |
| `0` | Success, help, or dry-run success |
| `1` | Runtime failure |
| `2` | Usage or parse error |
| `3` | Source, stage, manifest, or selected path not found |
| `4` | Manifest validation failed |

## Examples

Stage selected files and create an archive:

```bash
toolx-pack stage --src build/install --out dist/toolx \
  --include bin --include README.md --include LICENSE \
  --archive dist/toolx.tar --json
```

Review the plan without creating outputs:

```bash
toolx-pack plan --manifest pack.json --json
```

Create a tar from an existing staged tree:

```bash
toolx-pack archive --src dist/toolx --archive dist/toolx.tar --json
```

Refresh a stage tree and remove stale files:

```bash
toolx-pack stage --manifest pack.json --remove-extra --json
```

## Scope

The MVP supports deterministic tar creation only. Zip, compression, signing,
remote publish, dependency discovery, package fingerprints, and parallel scans
are intentionally deferred.
