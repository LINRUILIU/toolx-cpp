# ToolX Roadmap

> Audience: users and maintainers
> Status: Active direction, not a release promise
> Applies to: `v0.3.3` maintenance preparation and later evidence-led work
> Source of truth for: current product direction and deferred scope

## Now — prepare v0.3.3 maintenance

The current work belongs to a `v0.3.3` maintenance release candidate, not a new
`v0.4` product. It repairs composition, failure-reporting, replacement and recovery
contracts and improves the [automation guide](automation.md). The six CLIs and
thirteen library targets remain the product set. See the
[candidate notes](releases/v0.3.3.md) for visible corrections, migration costs and
release status; `v0.3.2` remains the released baseline until publication.

Maintenance includes accurate layer traces, ordered overlays, finite-number and
path-index checks, retained failure and rollback facts, native single-target
replacement, FSXJ4 recovery and logging-path isolation. These fixes do not establish
lower model adoption or verification costs. FSXJ4 has a real recovery-version,
storage and I/O cost; the [filesystem guide](modules/fsx.md) defines it.

Keep existing platform, installation, archive, coverage, TLS and documentation
gates. Historical local passes and observation binaries cannot substitute for
the current candidate's evidence. The
[contract record](development/audits/v0.4-contract-update.md) retains successive
findings, failures, permission skips and follow-up checks under its research label.

## What the task evidence supports

| Observation | Result and implication |
| --- | --- |
| [TinyXML2 delivery](development/audits/v0.4-task-slice.md) | Mature recipe, thin ToolX and minimal runner met the same checks; no incremental runner benefit was established. |
| [Configuration publication](development/audits/v0.4-config-slice.md) | Native tools, mature libraries and thin ToolX recipes passed 39 outcome checks; common project recipes carried the critical safeguards. |
| [Initial AI use](development/audits/v0.4-ai-observation.md) | Both attempts completed correctly; the ToolX attempt still wrote a shadow merge as an extra check. |
| [Caddy AI use](development/audits/v0.4-caddy-ai-observation.md) | Three valid conditions passed independent acceptance; none executed ToolX, so repaired-version verification or repair savings were not measured. |

These findings do not establish a new product case. The short guide addresses
observed discovery and acceptance-boundary friction, but its adoption benefit has
not been demonstrated. Historical audits, binaries and archives retain their
original identities rather than becoming `v0.3.3` release evidence.

## v0.4 — a direction with an admission gate

The direction remains lowering the adoption and trust costs of AI completing
real automation tasks. There is no committed `v0.4.0`/`v0.4.1` capability list or
delivery sequence. Agents, project scripts and mature systems continue to own
orchestration, native validation and service publication.

Admit a new responsibility only when repeated real use shows a concrete obstacle
and a bounded ToolX operation is materially easier to adopt and verify than mature
tools or a reusable script. Give alternatives the same project information,
script/cache reuse and permissions. Cover normal completion, a real failure and
stale artifacts or partial completion, then confirm benefit on a held-out task.
Count installation, discovery, extra verification, repair and ongoing maintenance;
do not credit a new architecture for a prewritten project recipe's benefit.

Without that evidence, continue maintenance without opening a new minor product
line. Configuration explanation, checked-byte publication and a unified runner
are candidates only if actual repeated duties justify them. None is a prerequisite
for further small AI-use observations. The
[internal decision](development/design/v0.4.x-mainline.md) records these limits.

## Maintained boundaries

- Improve admitted CLIs from reproducible reports and concrete workflows.
- Extract private helpers only when actual reuse justifies their maintenance.
- Keep `schemax` and `tuix` experimental until usage evidence supports a change.
- Add no CLI, general runner, deployment platform, secret system, full JSON
  Schema implementation, monitoring daemon or general TUI framework by default.
- Preserve documented `0.3.x` compatibility, with explicit correctness exceptions
  and migration guidance; ABI compatibility across build configurations remains
  outside the promise.

User-visible changes belong in [CHANGELOG.md](../CHANGELOG.md); release status
belongs in version-specific notes. Historical rationale stays in
[development materials](development/index.md).
