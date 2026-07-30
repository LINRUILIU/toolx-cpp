# ToolX Stability Boundary

> Audience: C++ consumers, CLI integrators, and maintainers
> Status: Normative specification
> Applies to: the `0.3.x` line
> Source of truth for: stability classification and compatibility promises

## Stability terms

| Level | Promise |
| --- | --- |
| Stable core | Documented public source APIs remain compatible within `0.3.x` unless a safety/correctness issue requires a break. |
| Stable support | Same source-compatibility intent, with a narrower helper-oriented role. |
| Bounded stable | The documented subset is supported; behavior outside the stated backend/product boundary is not implied. |
| Experimental MVP | Useful and tested, but API shape may change as the supported subset is proven. |
| Experimental foundation | Building blocks are available without a framework-level compatibility promise. |

ToolX does not promise ABI compatibility across compilers, standard libraries,
runtime linkage, build types, architectures, or TLS backend selections.

## Library classification

| Module | Level | Stable boundary |
| --- | --- | --- |
| `argtool` | Stable core | Parser builder, typed values, constraints, help, trace and JSON diagnostics |
| `cfgx` | Stable core | Node/path model, parsing, layering, validation, reload, snapshots and file adapters |
| `asyncx` | Stable core | Thread pool, scheduling, cancellation, task groups, wait helpers and metrics |
| `fsx` | Stable core | Batch plans, atomic writes, journals/recovery, directory sync, tar and watcher basics |
| `logsys` | Stable core | Logger, sinks, formatters, context, spans, profiles and metrics |
| `utils`, `sysx`, `hashx`, `textcodec` | Stable support | Documented helper and platform surfaces |
| `resultx` | Stable support | Inline normalization adapters; underlying modules retain their own contracts |
| `httpx` | Bounded stable | HTTP client behavior; HTTPS depends on the selected build-time backend |
| `schemax` | Experimental MVP | Compiled subset validation on top of `cfgx`; not full JSON Schema |
| `tuix` | Experimental foundation | Terminal primitives and MVP widgets; not a general application framework |

The module guides define the exact supported capability and non-goal for each
target.

## Product CLI classification

`toolx-config` is the primary stable CLI compatibility contract. The other five
installed tools are bounded-stable products with explicit workflow limits:

- `toolx-sync`: config composition and atomic publish;
- `toolx-pack`: local staging and deterministic tar creation;
- `toolx-http`: simple endpoint preflight;
- `toolx-log`: offline logsys-format diagnosis;
- `toolx-inspect`: bounded config/schema report and terminal view.

For tested commands, exit codes, JSON fields, side effects, and precedence, use
the [cross-CLI matrix](cli/matrix.md) and individual
[CLI references](cli/). Those documents—not library `Status` types—define CLI
compatibility.

Existing tested JSON envelope fields are additive-only within `0.3.x`. A field
may be added, but removing or redefining an existing field requires an explicit
compatibility decision and migration note.

## Correctness exceptions

A source-compatible implementation change may still alter unsafe or incorrect
behavior. When a safety or correctness fix requires a visible behavior change:

1. preserve input/output shape where safe;
2. add a regression or contract test;
3. document the change in `CHANGELOG.md` and release notes;
4. provide migration guidance when callers may have relied on the old behavior.

## Versioning

- `0.3.x` promises documented source compatibility, not ABI stability.
- Experimental modules may evolve within the minor line, but changes must be
  called out in the changelog.
- New CLI products do not enter the install set until they satisfy the internal
  productization checklist.
- The current repository snapshot is a `v0.3.2` release candidate; `v0.3.1` is
  the latest tagged release.

See [Architecture](architecture.md), [Build and compatibility](build-and-compatibility.md),
and [Dependencies](dependencies.md) for boundaries outside API stability.
