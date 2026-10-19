"""Reproduce bounded sync contract gaps against explicit, hashed binaries.

This script is experiment evidence, not a general task runner or a promise that
the observed gaps are desired behavior. It refuses an existing output directory.
Visibility sampling reports observations, never a failure probability; both
arms have one writer, the same directory permissions and no replacement locks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import threading
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


class Evidence:
    def __init__(self, root):
        self.root, self.commands = root, []
        root.mkdir(parents=True)

    def call(self, argv, label):
        argv = [str(arg) for arg in argv]
        started = time.perf_counter()
        proc = subprocess.run(argv, capture_output=True, timeout=30)
        stem = f"{len(self.commands):02}-{label}"
        (self.root / (stem + ".stdout")).write_bytes(proc.stdout)
        (self.root / (stem + ".stderr")).write_bytes(proc.stderr)
        record = {"argv": argv, "label": label, "exit": proc.returncode,
                  "seconds": time.perf_counter() - started,
                  "stdout": stem + ".stdout", "stderr": stem + ".stderr"}
        self.commands.append(record)
        save(self.root / "commands.json", self.commands)
        return proc


def envelope(proc):
    return json.loads(proc.stdout.decode("utf-8"))


def trace_probe(root, toolx):
    evidence = Evidence(root)
    base, first, second = root / "base.json", root / "first.json", root / "second.json"
    save(base, {"servers": ["a", "b"], "svc": {"host": "base", "port": 8080}, "gone": {"leaf": True}})
    save(first, {"servers": ["c"], "svc": {"host": "first"}, "gone": False})
    save(second, {"svc": {"port": 9090}})
    outputs = {}
    for mode in ("replace", "append"):
        out = root / (mode + ".json")
        argv = [toolx, "--json", "--base", base, "--overlay", first, "--overlay", second, "--out", out]
        if mode == "append":
            argv.append("--append-arrays")
        proc = evidence.call(argv, mode)
        result, actual = envelope(proc), json.loads(out.read_text(encoding="utf-8"))
        trace = {entry["path"]: entry["layer"] for entry in result["data"]["source_trace"]}
        expected_servers = ["c"] if mode == "replace" else ["a", "b", "c"]
        outputs[mode] = {"exit": proc.returncode, "output_sha256": digest(out),
                         "output": actual, "trace": trace,
                         "composition_correct": actual["servers"] == expected_servers and actual["gone"] is False,
                         "ghost_object_leaf": "gone.leaf" in trace,
                         "ghost_replaced_array_leaf": mode == "replace" and "servers[1]" in trace,
                         "append_origin_wrong": mode == "append" and trace.get("servers[0]") == "local",
                         "append_origin_missing": mode == "append" and "servers[2]" not in trace,
                         "overlay_files_indistinguishable": trace.get("svc.host") == trace.get("svc.port") == "local"}
    observed = (all(row["composition_correct"] and row["exit"] == 0 for row in outputs.values())
                and outputs["replace"]["ghost_object_leaf"]
                and outputs["replace"]["ghost_replaced_array_leaf"]
                and outputs["append"]["append_origin_wrong"]
                and outputs["append"]["append_origin_missing"]
                and outputs["replace"]["overlay_files_indistinguishable"])
    return {"current_behavior_reproduced": observed, "outputs": outputs}


def schema_probe(root, toolx):
    evidence = Evidence(root)
    base, schema, out = root / "base.json", root / "standard-schema.json", root / "out.json"
    save(base, {"port": 8080})
    save(schema, {"$schema": "https://json-schema.org/draft/2020-12/schema", "type": "object"})
    proc = evidence.call([toolx, "--json", "--base", base, "--schema", schema, "--out", out], "schema-header")
    result = envelope(proc)
    return {"current_behavior_reproduced": proc.returncode == 1 and not out.exists()
            and "$schema" in result["message"], "exit": proc.returncode,
            "output_exists": out.exists(), "envelope": result,
            "limit": "schemax explicitly documents a subset; this is an adoption limitation, not draft compliance promised by 0.3.x"}


def checked_candidate_probe(root, toolx, sing_box):
    evidence = Evidence(root)
    candidate, checked, current = root / "candidate.json", root / "checked-bytes.json", root / "current.json"
    config = {"inbounds": [{"type": "mixed", "tag": "in", "listen": "127.0.0.1", "listen_port": 30883}],
              "outbounds": [{"type": "direct", "tag": "direct"}]}
    save(candidate, config)
    before = evidence.call([sing_box, "check", "--disable-color", "-c", candidate], "check-before")
    checked.write_bytes(candidate.read_bytes())
    checked_sha = digest(checked)
    config["inbounds"][0]["listen_port"] = "bad"
    save(candidate, config)
    publish = evidence.call([toolx, "--json", "--base", candidate, "--out", current], "publish-after-edit")
    after = evidence.call([sing_box, "check", "--disable-color", "-c", current], "check-after")
    return {"current_behavior_reproduced": before.returncode == 0 and publish.returncode == 0 and after.returncode == 1,
            "checked_sha256": checked_sha, "edited_candidate_sha256": digest(candidate),
            "published_sha256": digest(current), "native_check_before": before.returncode,
            "sync_publish": publish.returncode, "native_check_after": after.returncode,
            "limit": "sync has no prior native-validation identity contract; this demonstrates why a shared byte-binding guard remains necessary"}


def partial_probe(root, toolx):
    evidence = Evidence(root)
    base, out = root / "base.json", root / "oldout.json"
    blocker, journal = root / "blocker", root / "failure.journal"
    snapshot = blocker / "snapshot.json"
    save(base, {"port": 8080})
    old = b'{"old":true}\n'
    out.write_bytes(old)
    blocker.write_text("not a directory", encoding="utf-8")
    argv = [toolx, "--json", "--base", base, "--out", out, "--snapshot", snapshot, "--journal", journal]
    dry = evidence.call([*argv, "--dry-run"], "dry-run")
    dry_unchanged = out.read_bytes() == old and not journal.exists() and not snapshot.exists()
    proc = evidence.call(argv, "second-write-fails")
    result = envelope(proc)
    restored = out.read_bytes() == old
    return {"current_behavior_reproduced": dry.returncode == 0 and dry_unchanged and proc.returncode == 1
            and restored and not journal.exists() and result["data"] == {},
            "dry_run_exit": dry.returncode, "dry_run_unchanged": dry_unchanged,
            "publish_exit": proc.returncode, "old_output_restored": restored,
            "snapshot_exists": snapshot.exists(), "journal_exists": journal.exists(),
            "failure_envelope": result, "output_sha256": digest(out),
            "extra_verification": "read oldout bytes and inspect journal/snapshot paths; failure envelope alone does not report rollback"}


def visibility_probe(root, toolx, writes):
    root.mkdir()
    base = root / "base.json"
    save(base, {"port": 8080})
    rows = []
    for arm in ("sync-journal", "os-replace"):
        evidence = Evidence(root / arm)
        out, journal = evidence.root / "current.json", evidence.root / "current.journal"
        out.write_text('{"old":true}', encoding="utf-8")
        stop = threading.Event()
        observation = {"stat_calls": 0, "file_not_found": 0, "other_errors": {}, "samples": []}

        def observe():
            while not stop.is_set():
                try:
                    out.stat()
                except FileNotFoundError as exc:
                    observation["file_not_found"] += 1
                    if len(observation["samples"]) < 3:
                        observation["samples"].append({"type": type(exc).__name__, "errno": exc.errno,
                            "winerror": getattr(exc, "winerror", None), "message": str(exc)})
                except OSError as exc:
                    key = f"{type(exc).__name__}:errno={exc.errno}:winerror={getattr(exc, 'winerror', None)}"
                    observation["other_errors"][key] = observation["other_errors"].get(key, 0) + 1
                observation["stat_calls"] += 1
                time.sleep(0.0002)

        thread = threading.Thread(target=observe)
        thread.start()
        operations, exits = [], []
        try:
            for index in range(writes):
                if arm == "sync-journal":
                    proc = evidence.call([toolx, "--json", "--base", base, "--out", out, "--journal", journal], f"write-{index:02}")
                    exits.append(proc.returncode)
                    if proc.returncode:
                        raise RuntimeError("visibility sync write failed; inspect captured evidence")
                else:
                    prepared = out.with_suffix(".prepared")
                    prepared.write_bytes(base.read_bytes())
                    operations.append({"step": index, "op": "os.replace", "src": str(prepared), "dst": str(out),
                                       "prepared_sha256": digest(prepared)})
                    os.replace(prepared, out)
                time.sleep(0.003)
        finally:
            stop.set()
            thread.join()
        save(evidence.root / "in-process-operations.json", operations)
        rows.append({"arm": arm, "writes": writes, "observations": observation,
                     "all_process_exits_zero": all(code == 0 for code in exits),
                     "final_output_correct": json.loads(out.read_text(encoding="utf-8")) == {"port": 8080},
                     "final_output_sha256": digest(out)})
    return {"current_behavior_reproduced": rows[0]["observations"]["file_not_found"] > 0
            and rows[1]["observations"]["file_not_found"] == 0
            and all(row["final_output_correct"] and row["all_process_exits_zero"] for row in rows),
            "rows": rows, "scope": "one writer and one reader per arm; no file sharing locks; same inherited directory permissions",
            "limit": "counts are sampled observations, not a failure probability or speed comparison; no power-loss, crash-durability, or multi-file atomicity claim"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolx", type=Path, required=True)
    parser.add_argument("--sing-box", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--visibility-writes", type=int, default=40)
    args = parser.parse_args()
    if args.visibility_writes < 1:
        parser.error("--visibility-writes must be positive")
    toolx, sing_box, root = args.toolx.resolve(), args.sing_box.resolve(), args.out.resolve()
    if not toolx.is_file() or not sing_box.is_file():
        parser.error("both explicit binary paths must exist")
    root.mkdir(parents=True, exist_ok=False)
    save(root / "environment.json", {"python": sys.version, "platform": platform.platform(),
        "script": str(Path(__file__).resolve()), "script_sha256": digest(Path(__file__)),
        "argv": sys.argv, "toolx": str(toolx), "toolx_sha256": digest(toolx),
        "sing_box": str(sing_box), "sing_box_sha256": digest(sing_box)})
    results = {"trace": trace_probe(root / "trace", toolx),
               "schema_header": schema_probe(root / "schema-header", toolx),
               "checked_candidate": checked_candidate_probe(root / "checked-candidate", toolx, sing_box),
               "partial_publish": partial_probe(root / "partial-publish", toolx),
               "visibility": visibility_probe(root / "visibility", toolx, args.visibility_writes)}
    save(root / "summary.json", results)
    print(json.dumps({name: result["current_behavior_reproduced"] for name, result in results.items()}, indent=2))
    if not all(result["current_behavior_reproduced"] for result in results.values()):
        raise SystemExit("a previously observed behavior did not reproduce; inspect summary, including whether a fix or sampling explains it")


if __name__ == "__main__":
    main()
