# ToolX Roadmap

> Audience: users and maintainers
> Status: Active direction, not a release promise
> Applies to: `v0.3.2` and subsequent maintenance
> Source of truth for: current product direction and deferred scope

## Now — maintain v0.3.2

The release completes deterministic proxy control, FSXJ3 recovery, filesystem/HTTP/
pack hardening and the documentation system. Keep the cross-platform test matrix,
OpenSSL loopback coverage, sanitizers/fuzz and release checks green as fixes arrive.
Track reproducible user reports within the documented `0.3.x` compatibility boundary.

## Next — consolidate the existing product line

- Improve ergonomics inside the six admitted CLIs based on concrete workflows.
- Track repeated JSON-envelope, manifest-validation and exit-code helpers as
  private implementation candidates.
- Keep install-tree, archive, compatibility and documentation verification
  aligned with the shipped product set.
- Gather usage evidence before changing the experimental status of `schemax`
  or `tuix`.

## Later — evidence-driven decisions

- Consider private shared CLI helpers only after another release confirms the
  contracts are stable.
- Consider a `0.4.0` design only when real workflows identify a missing public
  surface or require a deliberate compatibility break.
- Revisit live log following, richer config inspection, additional archive
  formats, and broader schema behavior only with a bounded product case.

## Not planned for 0.3.x

- Additional product CLIs
- Package management, deployment orchestration or remote publishing
- Secret storage or credential workflows
- Complete JSON Schema compatibility
- A general monitoring daemon or TUI application framework
- ABI compatibility across toolchains and build configurations

Completed user-visible work moves to [CHANGELOG.md](../CHANGELOG.md). Historical
rationale belongs in [development retrospectives](development/retrospectives/),
not in this active roadmap.
