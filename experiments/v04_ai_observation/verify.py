"""Independently accept actual observation artifacts, not agent self-reports."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "v04_config_slice"))
from experiment import Evidence, Origin, smoke
from http.server import ThreadingHTTPServer


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    expected = {"log": {"level": "debug"},
        "inbounds": [{"type": "mixed", "tag": tag, "listen": "127.0.0.1", "listen_port": port}
                     for tag, port in (("generated-in", 60431), ("user-in", 60432))],
        "outbounds": [{"type": "direct", "tag": "direct"}],
        "route": {"final": "direct", "rules": [
            {"inbound": ["user-in"], "action": "route", "outbound": "direct"},
            {"domain_regex": ["^review[.]invalid$"], "action": "route", "outbound": "direct"}]}}
    rows = []
    origin = ThreadingHTTPServer(("127.0.0.1", 0), Origin)
    thread = threading.Thread(target=origin.serve_forever, daemon=True)
    thread.start()
    try:
        for arm in ("baseline", "available"):
            home = root / arm
            brief = json.loads((home / "brief.json").read_text())
            binary = Path(brief["common_tools"]["sing_box"])
            current = home / "release/current.json"
            actual = json.loads(current.read_text())
            shape_ok = actual == expected
            old = json.loads((root / "predeclared.json").read_text())["input_sha256"]
            original_ok = hashlib.sha256((home / "inputs/00-generated.json").read_bytes()).hexdigest() == old["00-generated.json"]
            evidence = Evidence(root / "independent-oracle" / arm)
            evidence.call([binary, "check", "--disable-color", "-c", current], "native-check", "oracle")
            smoke(binary, current, evidence, origin.server_port, "oracle")
            row = {"condition": arm, "shape": shape_ok, "generated_input_unchanged": original_ok,
                "native_and_actual_owned_proxy": True, "current_sha256": hashlib.sha256(current.read_bytes()).hexdigest(),
                "commands": evidence.commands, "passed": shape_ok and original_ok}
            rows.append(row)
    finally:
        origin.shutdown()
        origin.server_close()
        thread.join(timeout=5)
    (root / "independent-oracle.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    print(json.dumps(rows, indent=2))
    if not all(row["passed"] for row in rows):
        raise SystemExit("independent artifact oracle failed")


if __name__ == "__main__":
    main()
