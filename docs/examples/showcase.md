# ToolX Example Showcase

> Audience: evaluators, CLI users, and C++ consumers
> Status: Verified example portal
> Applies to: `v0.3.2` release candidate
> Source of truth for: example learning order and evidence ownership

This showcase connects the 13 focused C++ cookbooks to one reproducible,
offline product workflow. Start with a module when evaluating an API; start
with the product chain when evaluating ToolX as an operator toolset.

## Learning routes

| Time | Route | Outcome |
| --- | --- | --- |
| 5 minutes | Read the [product-chain report](product-chain.md) | Understand how the six CLIs exchange files and machine-readable results |
| 15 minutes | Run `examples/product_chain_showcase/run.ps1` or `run.sh` | Reproduce the report in a disposable build directory |
| 30 minutes | Pick one row in the [cookbook report](module-cookbooks.md) | Learn a module's normal path, boundary behavior and side effects |
| 60 minutes | Run all 13 cookbooks, then modify a copied fixture | Compare library-level and product-level contracts |

## Capability map

| Concern | C++ modules | Product proof |
| --- | --- | --- |
| Parse and compose configuration | `argtool`, `cfgx`, `schemax` | `toolx-config` and `toolx-sync` |
| Files and packaging | `fsx`, `hashx`, `utils` | `toolx-pack` |
| Endpoint readiness | `httpx`, `resultx`, `sysx` | loopback `toolx-http check` |
| Diagnostics | `logsys`, `textcodec` | `toolx-log summarize` |
| Terminal inspection | `tuix` | `toolx-inspect report/render` |
| Scheduling and orchestration | `asyncx` | used by library consumers; intentionally not hidden behind a CLI |

## Evidence policy

- Cookbook output in this section was captured from the checked-in source,
  built with Ninja and Clang 21.1.0 on Windows for the `v0.3.2` candidate.
- The HTTP proof binds only to `127.0.0.1`, and the client invocation includes
  `--no-proxy-from-env`; no external endpoint is required.
- Runners copy fixtures into the explicit output directory before use. Raw
  output remains under `build/` or another caller-selected temporary path.
- The committed frame is the exact text produced by `toolx-inspect render`.
  Its SVG is a presentation of that transcript, not invented terminal output.

Return to the [documentation index](../index.md).
