# ToolX 0.3.0 Productization Retrospective

This retrospective records the release-readiness review for the first ToolX CLI
product chain. It is intentionally narrower than a roadmap: it answers what is
stable now, what was learned during productization, what risk remains, and what
should happen after `v0.3.0`.

## Version Story

- `v0.2.0` proved the single-tool product model with `toolx-config`: stable help
  text, exit codes, JSON envelope, install rules, package smoke, and release
  notes.
- The `0.2.x` development line admitted the remaining bounded-stable CLI
  products: `toolx-sync`, `toolx-pack`, `toolx-http`, `toolx-log`, and
  `toolx-inspect`.
- `v0.3.0` is the product-chain finalization release. It should not expand the
  CLI set further; it should freeze the current chain, document the contracts,
  and release the install/archive shape validated by CI.

## Stable Contracts

The current public CLI set is complete for the intended operational loop:

| CLI | Stable role | Contract status |
| --- | --- | --- |
| `toolx-config` | Config authoring and review | Primary CLI contract with stable envelope `toolx.config.result`. |
| `toolx-sync` | Config composition and publish | Bounded-stable publish workflow with validation, dry-run, snapshots, journals, and audit logs. |
| `toolx-pack` | Local staging and deterministic tar packaging | Bounded-stable staging/archive contract with manifest validation and install/archive smoke. |
| `toolx-http` | Runtime endpoint preflight | Bounded-stable loopback-tested HTTP gate; not a general `curl` replacement. |
| `toolx-log` | Offline runtime log diagnosis | Bounded-stable logsys text/JSON-lines analyzer and lightweight gate. |
| `toolx-inspect` | Config/schema terminal inspection | Bounded-stable report/render/scripted-run inspector; not a TUI framework. |

Each admitted CLI has:

- stable exit-code taxonomy
- `schema_version=1` JSON envelope
- black-box CLI contract tests
- install-tree smoke coverage
- packaged archive smoke coverage
- standalone reference documentation
- README or docs workflow examples

The stable core library surface remains `argtool`, `cfgx`, `asyncx`, `fsx`,
`logsys`, `resultx`, and support helpers. `schemax` remains an experimental MVP,
`httpx` remains bounded by backend configuration, and `tuix` remains an
experimental foundation used by a bounded product CLI.

## Product Closure

The product chain is coherent enough for `v0.3.0`:

1. Author and review config with `toolx-config`.
2. Compose and publish config with `toolx-sync`.
3. Stage and archive release files with `toolx-pack`.
4. Preflight runtime endpoints with `toolx-http`.
5. Diagnose runtime logs with `toolx-log`.
6. Inspect config/schema state in a terminal view with `toolx-inspect`.

The chain is intentionally not a package manager, deployment platform, secret
manager, full JSON Schema implementation, monitoring system, or TUI framework.
That boundary is healthy and should be preserved for the release.

## Technical Debt

No blocking technical debt was found during productization. The main debt is
structural repetition across CLIs:

- JSON envelope construction and plain key-value output helpers are duplicated.
- Manifest schema validation patterns are similar across product CLIs.
- Exit-code mapping is implemented per tool and needs careful contract tests.
- Release smoke scripts grow linearly with each admitted CLI.

These are acceptable for `v0.3.0` because the duplicated code is local,
contract-tested, and not yet large enough to justify a public API. A future
internal helper layer, for example `clix` or private tool helpers, should be
considered only after at least one more release proves the contracts are stable.

## Risks

- Version narrative risk: docs must consistently explain that `v0.2.0` was the
  single-tool stable baseline, while `v0.3.0` is the product-chain release.
- Compatibility risk: CLI JSON fields should remain additive-only through the
  `0.3.x` line; breaking changes need release notes and migration examples.
- Scope risk: adding more CLIs now would weaken the release. Future product work
  should be admitted only through the existing checklist.
- Backend risk: HTTPS behavior still depends on the selected `httpx` TLS
  backend. Release notes should keep that explicit.
- Framework risk: `toolx-inspect` uses `tuix`, but does not stabilize `tuix` as
  an application framework.

## Release Recommendation

`v0.3.0` is suitable for release after the standard gate passes on `main`:

- format check
- full build
- full test suite
- install-tree smoke
- install consumer build
- CPack binary archive
- unpacked archive smoke
- GitHub Actions matrix on Linux GCC, Linux Clang, Windows MSVC, and macOS Clang

No new CLI should be added before the release. Only release-blocking fixes,
documentation consistency fixes, and contract-test fixes should be accepted.

## Next Development Direction

After `v0.3.0`, prefer consolidation over expansion:

- Tighten release notes, examples, and versioned docs.
- Track duplicated CLI helper patterns as internal-helper candidates.
- Improve focused product ergonomics inside existing CLIs before admitting new
  tools.
- Consider a `0.3.x` patch line for contract hardening and bug fixes.
- Defer any `0.4.0` planning until real user workflows identify a missing
  product surface.
