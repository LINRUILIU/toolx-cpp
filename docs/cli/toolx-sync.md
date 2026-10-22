# toolx-sync CLI Reference

> Audience: configuration publishers and automation users
> Status: Bounded-stable CLI specification
> Applies to: the `0.3.x` released baseline through v0.3.4
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

The v0.3.3 release applies each overlay directly to the current composed
tree in command order. It no longer pre-merges overlays, which could resurrect
base children after an overlay replaced their object with a scalar and a later
overlay rebuilt that object. This corrects output in those type-changing cases;
all surviving local values still use the `local` source layer, without filenames.

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

The v0.3.3 release also preserves composed context on validation and
schema-load failures, including `source_trace`, `issues_count`,
`validation_passed`, `publish_attempted` and `stage`. Stages are `schema_load`,
`validation`, `dry_run` or `publish`; early argument/input-load errors do not
claim composed context. `source_trace` identifies source layers, not individual
overlay files. `--json` applies to help/parse errors even when it follows them.
Output-format discovery respects known option values and the `--` terminator;
a filename whose value is `--json` does not request JSON output.

After an actual publish attempt, `data.execution` is present on success and
failure. It records completed/skipped/rolled-back business-step counts, actual
step reports, and `rollback_attempted`, `rollback_complete`, `rollback_error`.
Inspect `rollback_attempted` before interpreting completeness. Each step retains
its forward `ok`/`error` independently of `rolled_back` and `rollback_error`.
Partial or failed rollback preserves evidence and does not claim restoration.
Consumers must handle additive data fields without relying on message text.
Corrected trace and rollback facts are explicit correctness changes; see the
[release compatibility and migration notes](../releases/v0.3.3.md), including
FSXJ4's inability to be read by older recovery tools.

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

Numeric range bounds must be finite and ordered; NaN, infinity and conversion
overflow are usage errors (exit `2`) before publishing. Unrepresentable JSON
numbers and invalid JSON number spellings are input-load errors (exit `1`).

## File behavior

Normal success creates/replaces `--out` and requested snapshot/journal/log
paths. Publishing uses overwrite conflict policy and best-effort rollback.
Before opening `--log-file`, sync rejects overlap with base/overlay/schema,
out/snapshot or journal paths as a usage error (exit `2`), without initializing
logging or publishing. The check includes normalized absolute paths, existing
symlink parents, hard-link aliases and Windows case-insensitive names, as well
as ancestor/descendant overlap. It assumes an exclusive caller; it is not a
concurrent-writer or adversarial path-mutation guard.
The boundary includes the actual size-rotation namespace, even while the log is
small: the current policy uses 10 MiB and five archives, `FILE.1` through `FILE.5`.
These paths cannot overlap any protected input, output or journal either.
Separate logs retain rotation; this fix does not disable retention or reject
unrelated suffixes such as `FILE.0` and `FILE.6` under the current policy.
With `--log-file`, any existing symbolic-link component in the log, its rotation
paths or a protected path must resolve to an existing target. Dangling or
unresolvable links are usage errors before initialization, including otherwise
separate dangling journal links. Ordinary new paths remain supported. Existing
valid links still undergo the same overlap check. Without `--log-file`, this
logging boundary does not change journal-link behavior.
Parent traversal (`..`) after a missing path component is also rejected when
logging is configured: publishing may create that directory before a later log
open, making the original path's boundary different from its normalized spelling.
On Windows, with logging configured, explicit stream syntax in any log/archive
or protected path component is also a usage error. This includes `FILE::$DATA`
and named streams, even when they are separate or the file does not yet exist.
Ordinary drive roots and extended-drive roots such as `\\?\C:\path\file` retain
their designator colon; later components still cannot contain stream syntax.
This logging-specific usage check does not run without logging. On POSIX, literal
colons in ordinary filenames retain their normal behavior.

Independently, when `--journal` is configured on Windows, `fsx` rejects explicit
stream paths in the journal or publish batch (`--out` and any `--snapshot`) before
the journal header or first mutation. This includes `FILE::$DATA`, named streams,
and otherwise separate or nonexistent stream paths. No `--log-file` is needed
to trigger this boundary. It is a publish error (exit `1`), with `ok=false`,
`publish_attempted=true`, stage `publish`, zero completed execution steps and no
rollback attempt: the publish API was called but rejected the plan before writing.
Normal and recognized extended-drive roots remain usable. Move previously accepted
stream paths to ordinary separate files; disabling logging does not bypass the
journal boundary. Without a journal, existing fsx behavior is unchanged, and
POSIX literal-colon paths remain ordinary filenames.
Dry-run does not run the fsx publish plan or write out/snapshot/journal. A
configured `--log-file` may be initialized while preparing logging; consult the
[cross-CLI matrix](matrix.md) when strict no-write behavior is required.

`--snapshot` writes a copy of this invocation's new resolved content; it is not
an old-output backup. Dry-run reads inputs (including an explicitly requested
remote URL), and neither binds a later invocation's bytes nor checks output
writability. Schema/policy success is local validation, not project-native or
service acceptance. To publish application-validated bytes, compose a private
candidate, use the application's native validation and behavior checks, and let
the project recipe publish that exact candidate. Re-running sync after checks
re-reads inputs and does not establish that identity.

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
For project-native acceptance and publication of checked bytes, start with the
[automation candidate recipe](../automation.md).
