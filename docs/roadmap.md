# ToolX Roadmap

> Audience: users and maintainers
> Status: Active direction, not a release promise
> Applies to: `v0.3.4` maintenance release and later evidence-led work
> Source of truth for: current product direction and deferred scope

## Now — maintain v0.3.4

`v0.3.4` repairs JSON data preservation, archive/download output isolation, HTTP
response handling and per-hop redirect policy, supported schema enforcement,
TaskGroup cancellation/outcomes and logging flush. The six CLIs and thirteen
library targets remain the product set. See the
[release notes](releases/v0.3.4.md) for corrections, rebuild requirements and
migration guidance. This maintenance release does not revive stopped v0.4 work.

The v0.3.3 composition, failure-reporting, native replacement and FSXJ4 recovery
repairs remain part of the maintained baseline. FSXJ4 retains its recovery-version,
storage and I/O costs; the [filesystem guide](modules/fsx.md) defines them.
No measured AI adoption or verification-cost benefit is claimed.

Keep existing platform, installation, archive, coverage, TLS and documentation
gates. Historical local passes and observation binaries cannot substitute for
the current release's evidence. The
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
