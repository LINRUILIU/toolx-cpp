# ToolX Cross-CLI Contract Matrix

> Audience: automation authors and maintainers
> Status: Canonical cross-product specification
> Applies to: the `0.3.x` line
> Source of truth for: CLI comparison, side effects, precedence and implicit behavior

## Global rules

- CLI exit codes are independent from C++ module `Status`/`Result` types.
- Every JSON envelope uses `schema_version=1` and the top-level `schema`,
  `schema_version`, `ok`, `code`, `message`, `issues`, and `data` shape.
- Existing tested envelope/data fields are additive-only in `0.3.x`.
- `--json` changes rendering, never business side effects.
- Explicit CLI arguments override manifest defaults.
- Dry-run guarantees are command-specific and must not be inferred for tools
  that do not expose dry-run.

## Commands and mutation

| CLI/command | Read inputs | Create/replace/remove | Dry-run guarantee |
| --- | --- | --- | --- |
| `toolx-config load/doctor/get/exists/validate/reload-dryrun` | Config/schema/current/candidate | None | Reload command is inherently read-only |
| `toolx-config set` | Config | Replaces config file | None |
| `toolx-config merge` | Base/overlay | Creates/replaces output | None |
| `toolx-config snapshot-export/restore` | Config/snapshot | Writes output; restore may replace input | None |
| `toolx-sync` | Base/overlay/schema; optional explicit remote URL | Output and optional snapshot/journal/log | Does not run publish or write out/snapshot/journal; log initialization may create its file |
| `toolx-pack stage` | Source tree/manifest | Stage tree, optional tar/journal/log; `--remove-extra` deletes stale stage entries | No stage/archive/journal/log writes |
| `toolx-pack archive` | Stage tree/manifest | Replaces tar, optional log | No archive/log writes |
| `toolx-pack plan` | Source tree/manifest | None | Equivalent to stage dry-run |
| `toolx-http check` | URL/manifest/body file | Optional audit log | Not available |
| `toolx-log summarize` | Log files/manifest | Optional audit log | Not available |
| `toolx-inspect report/render/run` | Config/schema/manifest | Optional audit log | Not available |

## Exit and schema matrix

| CLI | Exit codes | JSON schema | Distinguishing tested data |
| --- | --- | --- | --- |
| `toolx-config` | `0` success; `1` runtime; `2` usage; `3` not found; `4` validation | `toolx.config.result` | Command-specific; doctor checks/recommendations; schema issues |
| `toolx-sync` | `0` success/dry-run; `1` runtime; `2` usage; `4` validation | `toolx.sync.result` | Layers, proxy flag, outputs, plan/steps, source trace |
| `toolx-pack` | `0` success/dry-run; `1` runtime; `2` usage; `3` path; `4` manifest | `toolx.pack.result` | Selection counts, planned/completed steps, archive/capabilities |
| `toolx-http` | `0` success; `1` transport; `2` usage; `3` input path; `4` expectation | `toolx.http.result` | Proxy flag, aggregate and per-check results |
| `toolx-log` | `0` success; `1` read/runtime; `2` usage; `3` input path; `4` gate | `toolx.log.result` | Parse/filter/severity/time/sample summary |
| `toolx-inspect` | `0` success; `1` load/render/path; `2` usage; `3` input path; `4` schema | `toolx.inspect.result` | Node/path/schema summary and deterministic frame |

## Precedence and implicit behavior

| CLI | Precedence/default behavior that callers must know |
| --- | --- |
| `toolx-config` | Format follows extension; arrays replace unless append is requested; schema issues exit `4`; restore without `--out` replaces `--file`. |
| `toolx-sync` | Layer order is base → remote → overlays; remote format auto-detects from URL; environment proxy is on by default; publish uses overwrite + best-effort rollback. |
| `toolx-pack` | CLI lists replace manifest lists; excludes follow includes; no includes means all regular files; archive format is tar; remove-extra deletes stale stage entries. |
| `toolx-http` | GET and status `200:299` are defaults; URL and manifest are exclusive; CLI request expectations override checks; environment proxy is on unless disabled. |
| `toolx-log` | Auto format inspects the first nonblank line; exact and minimum level filters are exclusive; gates can turn a successful read into exit `4`. |
| `toolx-inspect` | Format follows extension; schema issues fail unless allowed; width/height define deterministic rendering; scripted run is bounded by ticks. |

## Network and global-state risk

| CLI | Network | Process-global state | Notes |
| --- | --- | --- | --- |
| `toolx-config` | None | Parser-adapter operations | File writes are command-explicit |
| `toolx-sync` | Only with `--remote-url` | Scoped cfgx remote fetcher and logger | Environment proxy defaults on; remote guard restores callback |
| `toolx-pack` | None | Optional logger | Highest filesystem mutation scope; review remove-extra and dry-run first |
| `toolx-http` | Explicit endpoint checks | Optional logger | Reported URLs redact sensitive query values |
| `toolx-log` | None | Optional logger | Reads only supported logsys formats |
| `toolx-inspect` | None | Logger and terminal state | No business output files |

The API-to-product audit that supports this matrix is maintained as internal
evidence under [development audits](../development/audits/).

### Tree boundary failures

Toolx-pack rejects linked source/stage descendants, including explicit includes.
Failed remove-extra enumeration fails planning rather than reporting partial success.
Artifact names resembling fsx temporary paths are preserved; cleanup only removes
transaction-owned paths.
