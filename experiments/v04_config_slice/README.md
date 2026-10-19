# Configuration publication comparison

> Audience: ToolX maintainers
> Status: Reproducible experiment, not a product interface
> Applies to: repository `24466b2`; sing-box `1.12.12`
> Source of truth for: commands and boundaries of the second v0.4 validation slice

The task is the JSON publication boundary of a real podkop consumer request:
[issue 280](https://github.com/itdoginfo/podkop/issues/280). Add user-managed
inbounds and routing rules to generated sing-box configuration, validate it,
and replace the active file only when the accepted bytes change. The issue was
closed as not planned; this experiment does not imply upstream adoption.

Three arms share input files, policy, local proxy smoke, publication guards,
repair recipe, caches and independent acceptance:

- `native`: sing-box merge, Python jsonschema policy, native check/start.
- `python`: mature deepmerge/jsonschema, native check/start.
- `toolx`: existing toolx-sync composition/policy into a private candidate,
  native check/start.

Final publication belongs to the shared recipe. Re-composing with sync after
native validation would not bind publication to the validated bytes. No runner
or task protocol is implemented. The Python code is an experiment harness;
its process logging and fault injection are not production features.

## Reproduce

Use Python 3.13 with `jsonschema==4.26.0`, CMake, and a supported C++ toolchain.
Build `toolx_sync` from the current checkout. Do not use a binary from another
worktree. Bootstrap downloads a pinned upstream Windows binary and checks the
SHA256 published in GitHub's release assets.

```powershell
python experiments/v04_config_slice/bootstrap.py temp/config-repro/deps
python -m pip install --no-deps --target temp/config-repro/python-deps deepmerge==2.0
python experiments/v04_config_slice/experiment.py --root temp/config-repro/results --sing-box temp/config-repro/deps/sing-box.exe --toolx PATH_TO_CURRENT_BUILD/toolx-sync.exe --python-deps temp/config-repro/python-deps
python experiments/v04_config_slice/probe_merge.py --toolx PATH_TO_CURRENT_BUILD/toolx-sync.exe --native temp/config-repro/deps/sing-box.exe --out temp/config-repro/merge-probes
python experiments/v04_config_slice/probe_contract.py --toolx PATH_TO_CURRENT_BUILD/toolx-sync.exe --sing-box temp/config-repro/deps/sing-box.exe --out temp/config-repro/contract-probes
```

Each result/probe directory must be new. The release checksum is pinned in
bootstrap.py; it does not depend on anonymous GitHub API quota. Download and
local executable/loopback access may require the execution environment's normal
approval. Nothing is installed globally by the commands above.

## Acceptance

The 13 cases per arm cover normal publication, unchanged repeat, bad type,
local policy rejection, invalid regex rejected by native check, missing default
outbound rejected only at start, occupied inbound, stale candidate, three forms
of drift, an actual Windows sharing violation, and actual repair/rerun.

The oracle reads current/candidate bytes and expected fault stage independently,
checks complete task semantics with narrowly defined native default
normalization, and runs native check/start against the published file. TCP
listener PID ownership binds proxy responses to the new child; cleanup checks
that child's listeners are gone. No external network request is made.

Repair is one shared, prewritten diagnostic/edit/rerun, not a blind model study.
Process output bytes are raw diagnostics, not token costs. Timings are single
observations, not benchmarks. Native merge and ToolX differ in scalar precedence,
file ordering, type handling and typed normalization; separate probes record
those differences instead of assuming interchangeability.

This validates Windows local JSON publication, not OpenWrt/UCI, router routing,
hot reload, distributed compare-and-swap, power-loss durability or all platforms.
See the [audit report](../../docs/development/audits/v0.4-config-slice.md) for
results, attribution and the resulting product decision.
