# ToolX 0.3.0 CLI Product Chain

`toolx-config` has already proved that ToolX can ship a usable CLI product instead of
only library examples. `v0.2.0` was the single-tool stable release and first CLI
product attempt. The `0.2.x` development line admitted the remaining
bounded-stable CLI products. `v0.3.0` finalizes that work into a coherent
workflow chain for developing, validating, shipping, and inspecting ToolX-based
utilities.

This document records the intended CLI product set. It is not an implementation
plan, and a tool should not be described as shipped until it satisfies
[cli_productization.md](cli_productization.md).

## Product Shape

The `0.3.0` CLI line follows the development path of a small operational tool:

1. create and review configuration
2. compose and publish configuration
3. stage distributable files
4. preflight runtime endpoints
5. inspect runtime logs
6. inspect configuration state interactively when a terminal view helps

The goal is a coherent toolchain, not a CLI wrapper for every library module.
Support modules such as `asyncx`, `resultx`, `utils`, `sysx`, `hashx`, and
`textcodec` should stay as libraries and cookbook examples unless a concrete
operator workflow needs a standalone command.

## Product Milestones

| Milestone | CLI | Product role | MVP boundary |
| --- | --- | --- | --- |
| `0.2.0` | `toolx-config` | Config authoring and review | Establish the first product CLI: inspect, edit, merge, validate, snapshot, and dry-run config changes with stable JSON output. |
| `0.2.x` | `toolx-sync` | Config composition and publish | Promote the scenario CLI into a product CLI for base config, overlays, optional remote layer, validation, dry-run publish reports, atomic output, snapshot, and audit log. |
| `0.2.x` | `toolx-pack` | Local staging and packaging | Stage a source directory into a release-shaped output tree and produce deterministic archive output for small ToolX utilities. |
| `0.2.x` | `toolx-http` | Runtime endpoint preflight | Check one or more endpoints with expected status, retry/timeout controls, and JSON output suitable for release smoke and deployment gates. |
| `0.2.x` | `toolx-log` | Runtime log inspection | Summarize text or JSON-lines logs by level, parse failures, and simple time/window filters for local diagnosis. |
| `0.2.x` | `toolx-inspect` | Terminal inspection | Provide a bounded terminal inspection surface for config files and schema issues; keep `tuix` itself experimental unless a separate framework decision is made. |
| `0.3.0` | Product chain | Release finalization | Freeze the current six-CLI chain as the public product set, with release notes, retrospective, install/archive smoke coverage, and CI-green main. |

## Current Admission State

`toolx-config`, `toolx-sync`, `toolx-pack`, `toolx-http`, `toolx-log`, and
`toolx-inspect` satisfy the admission bar for the current product chain.
`toolx-inspect` is documented in [toolx-inspect.md](toolx-inspect.md) and has
its own installed target, black-box contract tests, install/archive smoke
coverage, standalone reference, and workflow example.

The cross-CLI command, dependency, side-effect, and risk matrix is recorded in
[product_cli_matrix.md](product_cli_matrix.md).
The API-to-product gap audit is recorded in
[api_product_gap_audit.md](api_product_gap_audit.md).

## Admission Rules

A CLI enters the public product chain only when it has:

- a documented product boundary and stability level
- an installed executable under `TOOLX_BUILD_TOOLS`
- stable exit codes and JSON envelope
- black-box CLI contract coverage
- install-tree and archive smoke coverage
- a standalone reference page under `docs/`
- at least one user-facing workflow example

Until those conditions are true, the tool remains a candidate. Documentation
should say "candidate" or "planned" rather than implying release availability.

## Explicit Non-Goals

The `0.3.0` line should not try to become a general package manager, deployment
system, secret manager, full JSON Schema implementation, or TUI application
framework. Those would pull the release away from the near-term product chain:
config, publish, package, preflight, and diagnose.
