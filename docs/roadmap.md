# ToolX Roadmap

> Audience: users and maintainers
> Status: Manual maintenance ended
> Applies to: v0.3.5 final release and retained history
> Source of truth for: project lifecycle and closed product direction

## Final release

**Manual maintenance ended on 2026-10-25 with v0.3.5.** No further manual bug
fixes, security fixes, feature development or issue triage are planned. Source,
documentation, tests and historical releases remain available for consumers and
fork maintainers. Existing CI is retained for reproducibility; it does not imply
continuing maintenance.

The final release repairs configuration persistence, numeric boundaries,
filesystem data loss and archive isolation, HTTP deadlines and parsing, task
waiting, logging and CLI input protection. The thirteen libraries and six CLIs
remain available. Read the [release notes](releases/v0.3.5.md) for migration and
rebuild requirements, and the [maintenance audit](development/audits/maintenance-20261025.md)
for the actual review and verification scope.

## Retained boundaries

The [stability policy](stability.md), [filesystem guide](modules/fsx.md),
[HTTP guide](modules/httpx.md) and [CLI matrix](cli/matrix.md) describe the retained
contracts. They do not promise future updates. FSXJ4 recovery/storage costs,
optional TLS selection, experimental schema/TUI scope and source-versus-ABI
compatibility limits remain applicable.

## Closed research direction

The previous v0.4 exploration did not establish sufficient evidence to launch a
new product line. No v0.4 workflow, runner, agent system or replacement SDK is
being committed to here. Historical research is retained under its original
identity, including the [task comparison](development/audits/v0.4-task-slice.md),
[configuration comparison](development/audits/v0.4-config-slice.md),
[AI observations](development/audits/v0.4-ai-observation.md),
[Caddy observation](development/audits/v0.4-caddy-ai-observation.md) and
[design record](development/design/v0.4.x-mainline.md). These are historical
findings, not evidence for unexecuted release checks or an active roadmap.

User-visible release history is in [CHANGELOG.md](../CHANGELOG.md); retained
implementation rationale is in the [development material](development/index.md).
