"""Prepare equal task inputs for small, exploratory AI-use observations.

Agents receive the brief and their own directory, never this setup or prior
experiment recipes. This is task preparation, not a model-evaluation runner.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=False)
    repo = Path(__file__).resolve().parents[2]
    fragments = {
        "00-generated.json": {"log": {"level": "warn"}, "inbounds": [
            {"type": "mixed", "tag": "generated-in", "listen": "127.0.0.1", "listen_port": 60431}],
            "outbounds": [{"type": "direct", "tag": "direct"}]},
        "10-user.json": {"inbounds": [{"type": "mixed", "tag": "user-in", "listen": "127.0.0.1", "listen_port": 60432}]},
        "20-route.json": {"route": {"final": "gateway", "rules": [
            {"inbound": ["user-in"], "action": "route", "outbound": "direct"}]}},
        "30-site.json": {"log": {"level": "debug"}, "route": {"rules": [
            {"domain_regex": ["^review[.]invalid$"], "action": "route", "outbound": "direct"}]}}}
    brief = {
        "task": "Maintain repeatable sing-box generated+user configuration publication",
        "requirements": [
            "Compose four inputs in filename order. Recursive objects, later scalar wins, arrays append in order.",
            "Keep both mixed inbounds and the two route rules. Site debug logging must override generated warn.",
            "Diagnose and repair any existing configuration issue; do not remove required functionality.",
            "Only 127.0.0.1 listeners. All traffic must use the available direct outbound.",
            "Deliver a reusable script and accepted release/current.json; do not edit 00-generated.json.",
            "Use actual application acceptance and local behavior to justify publication. Do not trust an old output as current success.",
            "Exercise a real invalid config and a publication failure, showing the last accepted file is preserved.",
            "Record commands/raw output, files/docs consulted, extra checks, dead ends, repairs and tool-choice reasons in observation.json.",
            "No external network requests, system proxy changes, production repo edits or generic runner."],
        "common_tools": {"sing_box": str(repo / "temp/v04-config-20261018/deps/sing-box.exe"),
            "python_deps": str(repo / "temp/v04-config-20261018/python-deps"),
            "python": "3.13; jsonschema 4.26.0 installed; deepmerge 2.0 in python_deps",
            "other": "PowerShell, Python standard library, CMake, rg; native sing-box help is available"}}
    for arm in ("baseline", "available"):
        home = root / arm
        for name, value in fragments.items():
            save(home / "inputs" / name, value)
        save(home / "release/current.json", {"log": {"disabled": True}, "outbounds": [{"type": "direct", "tag": "direct"}]})
        save(home / "brief.json", brief)
        if arm == "available":
            binary = repo / "temp/v04-config-contract-build-v2/Release/toolx-sync.exe"
            (home / "tools").mkdir()
            shutil.copyfile(binary, home / "tools/toolx-sync.exe")
            for name in ("docs/cli/toolx-sync.md", "docs/modules/schemax.md", "docs/modules/cfgx.md", "docs/modules/fsx.md"):
                data = subprocess.check_output(["git", "show", "HEAD:" + name], cwd=repo)
                path = home / "toolx-docs" / Path(name).name
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(data)
    save(root / "predeclared.json", {"kind": "exploratory qualitative AI-use observation, not statistical/token evaluation",
        "conditions": ["baseline: mature tools", "available: same facts and tools plus optional existing ToolX"],
        "task": brief, "input_sha256": {name: hashlib.sha256((root / "baseline/inputs" / name).read_bytes()).hexdigest() for name in fragments},
        "toolx_sha256": hashlib.sha256((root / "available/tools/toolx-sync.exe").read_bytes()).hexdigest(),
        "limits": ["one agent attempt per condition initially", "no token measurements", "self-recorded actions checked against raw outputs and artifacts", "no prior experiment recipes or fault answer supplied"]})
    print(root)


if __name__ == "__main__":
    main()
