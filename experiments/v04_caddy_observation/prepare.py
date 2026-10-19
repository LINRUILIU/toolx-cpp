"""Freeze identical Caddy tasks after the candidate's validation gates pass.

No model call, task solution, publisher, or expected fault-location answer is
supplied. Three independent attempts distinguish mature tools, released ToolX,
and the repaired candidate. Each is free to decline ToolX.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fixed-binary", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=False)
    repo = Path(__file__).resolve().parents[2]
    old_binary = repo / "temp/v04-ai-20261018/available/tools/toolx-sync.exe"
    fixed_binary = args.fixed_binary.resolve()
    base = {"admin": {"listen": "127.0.0.1:61481", "config": {"persist": False}},
            "apps": {"http": {"servers": {"local": {
                "listen": ["127.0.0.1:61480"], "automatic_https": {"disable": True},
                "routes": [{"handle": [{"handler": "static_response", "body": "generated old release"}]}]}}}}}
    spa = {"@id": "app_spa", "handle": [{"handler": "static_response", "body": "spa-v2"}], "terminal": True}
    api = {"@id": "app_api", "match": [{"path": ["/api/*"]}], "terminal": True,
           "handle": [{"handler": "reverse_proxy", "upstreams": [{"dial": "127.0.0.1:61482"}]}]}
    host = {"@id": "app_site", "match": [{"host": ["app.test"]}],
            "handle": [{"handler": "subroute", "routes": [spa, api]}], "terminal": True}
    health = {"@id": "health_site", "match": [{"host": ["health.test"]}], "terminal": True,
              "handle": [{"handler": "static_response", "body": "ready-v2"}]}
    inputs = {"00-generated.json": base,
              "10-environment.json": {"logging": {"logs": {"default": {"level": "INFO"}}}},
              "20-sites.json": {"apps": {"http": {"servers": {"local": {"routes": [host, health]}}}}}}
    common = {
        "task": "Repeatable Caddy JSON route release: insert API before SPA, preserve route IDs and other host",
        "basis": "Real Caddy JSON/API route-order maintenance described in caddyserver/caddy#6770; adapted to loopback and static fixture responses. Not an upstream patch or production rollout.",
        "requirements": [
            "Compose inputs/*.json in filename order. Objects recurse; later scalar wins; arrays REPLACE. Keep 00-generated.json byte-identical. Repair site input as needed, keeping all IDs and both hosts.",
            "GET /api/ping with Host app.test must reach the local origin and return api-v2:/api/ping; GET / and /app/deep with Host app.test return spa-v2; Host health.test returns ready-v2. Check actual Caddy behavior, not only JSON validity.",
            "Deliver reusable Python entry publish.py accepting --inputs-dir, --current, --accepted. Defaults: inputs, release/current.json, release/accepted.json. Both outputs must contain identical, newly accepted JSON bytes. A script wrapper may call any mature tool and may reject ToolX.",
            "Use Caddy's own validator and isolated runtime acceptance before releasing. The native API/reload mechanisms are available; choose responsibilities yourself. Run local origin fixture with python origin.py. Stop only owned processes when done.",
            "Reject a real native-invalid candidate, preserving both previous accepted files. Exercise a real OS publication failure (e.g. second destination opened without delete-sharing) with a candidate different from previous; preserve both accepted files and diagnostic evidence. Preserve a successful candidate then cause a later compose/validation failure: stale output must not count as current success.",
            "No external traffic, global installs, production deployment or generic runner. Listeners only 127.0.0.1; use allocated ports 61480/61481/61482; sequential attempts share these ports.",
            "Observation instrumentation, identical for all arms: on EVERY publish.py invocation print a final single-line JSON receipt with ok (boolean), stage (compose, validation, acceptance, publish, or complete), candidate_sha256 (SHA-256 of exact candidate bytes, null before composition), publish_attempted (boolean), and error (raw diagnostic string, empty on success). Stage identifies where failure occurred. Retain those exact candidate bytes and native command argv/exit/stdout/stderr under the directory in TOOLX_OBSERVATION_EVIDENCE, or a fresh evidence subdirectory when that environment variable is absent. Preserve raw OS exception/tool error diagnostics, including numeric error codes when available. This is study instrumentation for the project script, not a ToolX interface requirement.",
            "The independent observer will also use valid alternate input directories and alternate output paths, then native-invalid, malformed-input, and locked-second-destination cases. Honor all three arguments. A nonzero exit alone is not proof that the intended failure stage was reached; an uncorroborated failure is inconclusive.",
            "Keep actual commands/output in evidence/ and action records in observation.json. For extra checks distinguish project-native acceptance from checks added to distrust the merge/publish tool; include reason, command/evidence, result. Record repair edits, reruns, dead ends and tool-choice reasons. Do not infer token cost from command counts."
        ],
        "tools": {"caddy": str(repo / "temp/v041-caddy-deps/caddy.exe"),
                  "python_deps": str(repo / "temp/v04-config-20261018/python-deps"),
                  "available": "Python standard library, jsonschema 4.26.0, deepmerge 2.0 at python_deps, PowerShell, rg, curl.exe. Same caches available to all attempts."},
        "references": ["https://github.com/caddyserver/caddy/issues/6770",
                       "https://caddyserver.com/docs/command-line", "https://caddyserver.com/docs/api"]}
    # Pinned order is declared before execution; one attempt per arm, no random
    # population claim. Fixed-before-old avoids always putting the new arm last.
    order = ["mature", "fixed", "released"]
    for arm in order:
        home = root / arm
        for name, value in inputs.items():
            save(home / "inputs" / name, value)
        save(home / "release/current.json", base)
        save(home / "release/accepted.json", base)
        shutil.copyfile(Path(__file__).with_name("origin.py"), home / "origin.py")
        brief = dict(common)
        if arm != "mature":
            brief["optional_tool"] = "tools/toolx-sync.exe and toolx-docs/docs/cli/toolx-sync.md; use only if useful. ToolX does not replace Caddy validation/runtime acceptance."
            (home / "tools").mkdir()
            shutil.copyfile(fixed_binary if arm == "fixed" else old_binary, home / "tools/toolx-sync.exe")
            for name in ("docs/cli/toolx-sync.md", "docs/modules/cfgx.md", "docs/modules/fsx.md", "docs/modules/schemax.md"):
                data = (repo / name).read_bytes() if arm == "fixed" else subprocess.check_output(["git", "show", "HEAD:" + name], cwd=repo)
                path = home / "toolx-docs" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            if arm == "fixed":
                shutil.copyfile(repo / "docs/automation.md", home / "toolx-docs/docs/automation.md")
                brief["optional_guide"] = "toolx-docs/docs/automation.md"
        save(home / "brief.json", brief)
    save(root / "predeclared.json", {"kind": "qualitative fresh-context AI observation; not a token benchmark",
         "order": order, "attempts_per_arm": 1, "task": common,
         "candidate_is_unreleased": True,
         "source_sha256": {name: hashlib.sha256((repo / name).read_bytes()).hexdigest() for name in
             ("src/argtool.cpp", "src/cfgx.cpp", "src/fsx.cpp", "include/fsx.h", "include/resultx.h", "tools/toolx_sync.cpp")},
         "caddy_dependency": json.loads((repo / "temp/v041-caddy-deps/upstream.json").read_text()),
         "guide_sha256": {arm: {path.relative_to(root / arm / "toolx-docs").as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in sorted((root / arm / "toolx-docs").rglob("*.md"))} for arm in ("fixed", "released")},
         "input_sha256": {name: hashlib.sha256((root / "mature/inputs" / name).read_bytes()).hexdigest() for name in inputs},
         "released_binary_sha256": hashlib.sha256(old_binary.read_bytes()).hexdigest(),
         "fixed_binary_sha256": hashlib.sha256(fixed_binary.read_bytes()).hexdigest(),
         "limits": ["one inherited-host model attempt per condition; model identifier/token accounting unavailable",
                    "same project facts, mature libraries, native validator and local fixture for all arms",
                    "fixed arm includes repaired binary and current bounded guide; effect cannot isolate code from documentation",
                    "Caddy route ordering is the upstream-derived task; dual-file preservation and explicit JSON layer policy are added contract stress probes, not claimed upstream requirements",
                    "common script receipts and retained diagnostics are study instrumentation; they corroborate execution but are not a public ToolX protocol or complete syscall trace",
                    "raw outputs/artifact checks corroborate self reports; not a complete transcript",
                    "new task differs from first observation; do not attribute between-task action differences to fixes"]})
    print(root)


if __name__ == "__main__":
    main()
