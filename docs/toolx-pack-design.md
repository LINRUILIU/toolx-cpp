# toolx-pack Product Design

This document records the design rationale that led to the bounded-stable
`toolx-pack` CLI. The shipped contract is documented in
[toolx-pack.md](toolx-pack.md); keep that reference as the source of truth for
operators and release gates.

The intended role is local staging and deterministic packaging for small
ToolX-based utilities.

## Scenarios

`toolx-pack` should handle these operator workflows:

- Stage an already-built utility into a release-shaped directory.
- Copy binaries, config templates, docs, licenses, and static assets into stable
  locations.
- Produce a dry-run report before changing the staging directory.
- Re-stage after a rebuild without leaving stale files behind.
- Produce a deterministic tar archive from the staged tree.
- Emit a machine-readable manifest for release smoke checks.
- Keep a journal so failed staging can be diagnosed or recovered.

The first MVP should not become a package manager. It should not resolve
third-party dependencies, sign artifacts, build installers, publish to remote
registries, manage secrets, or promise cryptographic checksums.

## Module Composition

`toolx-pack` should compose existing ToolX modules:

| Module | Use |
| --- | --- |
| `argtool` | CLI parsing, help, stable usage errors |
| `cfgx` | Optional pack manifest loading and JSON output assembly |
| `schemax` | Manifest validation for the supported manifest subset |
| `fsx` | Directory walk, sync plan, atomic writes, journal, tar archive |
| `logsys` | Optional audit log for staging and archive actions |
| `asyncx` | Optional parallel scan/hash work after the serial MVP is stable |
| `hashx` | Non-cryptographic content fingerprints inside local manifests only |
| `resultx` | Internal error normalization if shared helpers become useful |

No new public C++ module API is required for the MVP. If shared code becomes
necessary, keep it private to `tools/` first and promote it only after the CLI
contract is stable.

## CLI Surface

Recommended MVP command shape:

```bash
toolx-pack stage --src build/install --out dist/toolx \
  --archive dist/toolx.tar --manifest pack.json --json
```

Commands:

| Command | Purpose |
| --- | --- |
| `stage` | Build or refresh the staged release tree; optionally archive it |
| `archive` | Create an archive from an existing staged tree |
| `plan` | Print the staging/archive plan without writing files |

The MVP can implement `plan` as `stage --dry-run` internally. Keeping the named
command is useful for release automation readability.

Core options:

| Option | Purpose |
| --- | --- |
| `--src DIR` | Source tree to stage |
| `--out DIR` | Staging directory |
| `--archive FILE` | Optional deterministic tar archive output |
| `--manifest FILE` | Optional pack manifest |
| `--name TEXT` | Package name override |
| `--version TEXT` | Package version override |
| `--include PATH` | Include explicit path; repeatable |
| `--exclude PATTERN` | Exclude relative path pattern; repeatable |
| `--remove-extra` | Remove files from stage that are absent from source/manifest |
| `--dry-run` | Report planned actions without writing |
| `--journal FILE` | Optional `fsx` journal |
| `--log-file FILE` | Optional audit log |
| `--json` | Emit machine-readable result |

## Manifest Shape

The manifest should be practical and small:

```json
{
  "name": "my-tool",
  "version": "1.0.0",
  "source": "build/install",
  "stage": "dist/my-tool",
  "archive": "dist/my-tool.tar",
  "include": ["bin/my-tool", "README.md", "LICENSE"],
  "exclude": ["**/*.pdb", "**/.DS_Store"],
  "remove_extra": true
}
```

CLI options should override manifest fields. The schema should reject unknown
top-level fields during MVP validation so typos are caught early.

## JSON Contract

Use a separate envelope:

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

Recommended exit codes:

| Code | Meaning |
| --- | --- |
| `0` | Success, help, or successful dry-run |
| `1` | Runtime error |
| `2` | Usage or parse error |
| `3` | Source/stage path not found |
| `4` | Manifest validation failed |

Recommended `data` fields:

- `command`
- `source`
- `stage`
- `archive`
- `manifest`
- `dry_run`
- `remove_extra`
- `entries`
- `bytes`
- `planned_steps`
- `completed_steps`
- `archive_format`
- `capabilities`
- `warnings`

## Admission Checklist

`toolx-pack` entered the public product chain after adding:

- installed target under `TOOLX_BUILD_TOOLS`
- black-box CTest for help, JSON, dry-run, stage success, archive success, and
  at least one failure path
- install-tree and archive smoke coverage
- standalone `docs/toolx-pack.md` reference
- README workflow example
- stability entry that clearly says whether it is bounded stable or
  experimental
