# Caddy AI-use observation after contract review

> Audience: ToolX maintainers
> Status: Small qualitative observation tooling, not a model benchmark or runner
> Applies to: Reviewed v0.4 candidate and frozen 24466b2 comparison
> Source of truth for: Task preparation, common oracle and interpretation limits

The real task is Caddy JSON route maintenance: place a reverse-proxy API route
before an SPA fallback, preserve IDs and another host, then validate and test the
actual application. Its basis is [Caddy issue #6770](https://github.com/caddyserver/caddy/issues/6770).
The issue was a support question closed as not planned; this is an adapted local
consumer task, not an upstream fix. All listeners and traffic are loopback.

The natural task already has a strong mature competitor: Caddy validation and
the native configuration API. The extra dual-file accepted-copy requirement is
an explicit contract stress probe. It must not be attributed to the upstream
user or presented as a reason to replace Caddy's own update/rollback mechanism.

Three fresh contexts run sequentially in a predeclared order: mature tools,
optional repaired ToolX, optional released ToolX. They share the same source
inputs, brief, dependencies, origin fixture, permission scope and script/cache
reuse. No publisher, previous task solution or fault-location answer is provided.
Both ToolX arms may decline the tool. The candidate arm includes its current
bounded guide, so this comparison observes the binary/documentation package.

The first candidate context read sibling completion metadata through the team
listing before implementation. That attempt is retained as `fixed`, with all
costs and outcomes, but excluded from the primary adoption comparison. A new
`fixed_clean` context receives the original frozen identities; the primary arms
are mature, fixed_clean and released. `protocol-deviation.json` preserves this
change without rewriting the initial declaration. Never call team/thread
inventory or retrieval tools from an observation context.

Prepare only after the reviewed build's local validation gates close:

```powershell
python experiments/v04_caddy_observation/bootstrap.py --deps temp/v041-caddy-deps
python experiments/v04_caddy_observation/prepare.py --root temp/caddy-observation-new --fixed-binary temp/v040-build/Release/toolx-sync.exe
```

Give each fresh agent only its own brief and directory. Do not expose the
preparation/oracle source, sibling attempts, prior experiments or this README.
Allow ordinary approval for local native process/network execution. Preserve
raw commands and outputs, scripts, receipts, repaired inputs and observation
records. Dispatch is manual collaboration tooling; no model API is built here.

The common independent oracle checks the final bytes, required route IDs,
generated-input identity, Caddy's native validator, four real HTTP responses,
listener PID ownership and cleanup. It also invokes the reusable entry on
alternate paths and a different valid candidate before judging failures. Native
invalid input, stale compose output and an actual locked publication destination
are common negative probes. A rejected command alone is not proof of correct
failure handling or of reaching the publication stage.

```powershell
python experiments/v04_caddy_observation/verify.py --root temp/caddy-observation-new --arm mature
```

Interpret actions by their purpose: required project-native acceptance; extra
checks added to verify ToolX's own claim; diagnosis/repair edits and reruns; or
observation instrumentation. Compare the same-task conditions first. A previous
sing-box observation is a source of hypotheses, not a causal before/after cost
baseline. Raw outputs and artifacts corroborate agent self-reports; they do not
provide a complete transcript or token accounting. One attempt per condition
cannot establish statistical advantages, general adoption or lower model costs.

The completed primary conditions all passed the independent oracle. None
executed ToolX; both optional ToolX conditions declined it. The repaired
package's effect on execution/repair cost therefore
remains unmeasured. Each added a synthetic merge fixture; required project
acceptance and rollback checks are not generic ToolX distrust. Read the
[completed audit](../../docs/development/audits/v0.4-caddy-ai-observation.md) for
retained failures, descriptive costs, isolation deviation and the decision to
keep the product light. `collect.py --root <observation-root>` creates an archive
with a checked per-file SHA-256 manifest after all four attempts have records
and independent verdicts.
