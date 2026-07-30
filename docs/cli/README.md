# ToolX Product CLI References

> Audience: CLI users, operators, and automation authors
> Status: Canonical product reference index
> Applies to: `v0.3.2` release candidate
> Source of truth for: CLI reference ownership

| CLI | Stability | Workflow | JSON schema |
| --- | --- | --- | --- |
| [`toolx-config`](toolx-config.md) | Primary stable contract | Config authoring, review and snapshots | `toolx.config.result` |
| [`toolx-sync`](toolx-sync.md) | Bounded stable | Layer composition and atomic publish | `toolx.sync.result` |
| [`toolx-pack`](toolx-pack.md) | Bounded stable | Local staging and deterministic tar | `toolx.pack.result` |
| [`toolx-http`](toolx-http.md) | Bounded stable | HTTP endpoint preflight | `toolx.http.result` |
| [`toolx-log`](toolx-log.md) | Bounded stable | Offline log diagnosis and gates | `toolx.log.result` |
| [`toolx-inspect`](toolx-inspect.md) | Bounded stable | Config/schema report and terminal rendering | `toolx.inspect.result` |

Use the [cross-CLI matrix](matrix.md) to compare exit codes, precedence,
network behavior, file writes and dry-run guarantees.

Library `Status`/`Result` types do not define CLI exit codes. Each product owns
its envelope and tested output contract.
