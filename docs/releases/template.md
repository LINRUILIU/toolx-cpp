This release ships the current ToolX install set and uses GitHub Release
archives produced from `cmake --install` output.

- Stable core modules remain the boundary described in `docs/stability.md`.
- `toolx-config` is the primary shipped CLI contract for the `0.1.x` line.
- `toolx-config` keeps the additive-only `toolx.config.result` / `schema_version=1` JSON envelope, including operator-facing diagnostics such as `doctor`.
- `toolx-sync` remains the scenario CLI for end-to-end composition and publish flows.
- ToolX does not promise ABI compatibility in `0.1.x`; the goal is source compatibility for the stable surface unless a safety or correctness bug requires a break.
- HTTPS behavior depends on the selected `httpx` TLS backend at build time.
