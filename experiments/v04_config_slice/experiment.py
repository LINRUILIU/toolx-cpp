"""Three reusable recipes for a real sing-box configuration publication boundary.

This is an experiment harness, not a generic task runner. All arms share project
knowledge, native acceptance, input freezing, publication guards and the oracle.
"""
from __future__ import annotations

import argparse
import contextlib
import ctypes
import hashlib
import http.server
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import zipfile

import jsonschema

NAMES = ["00-generated.json", "10-extra-inbound.json", "20-route.json", "30-site.json"]
SCENARIOS = ["normal", "repeat", "type_failure", "policy_failure", "native_failure", "startup_failure", "occupied_inbound",
             "stale_candidate", "candidate_changed", "input_changed", "target_changed",
             "publish_io_failure", "repair"]
POLICY = {"type": "object", "required": ["inbounds", "outbounds", "route"], "properties": {
    "inbounds": {"type": "array", "items": {"type": "object",
        "required": ["type", "tag", "listen", "listen_port"], "properties": {
            "listen": {"enum": ["127.0.0.1"]},
            "listen_port": {"type": "integer", "minimum": 1, "maximum": 65535}}}}}}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def inventory(root):
    return {name: sha(root / name) for name in NAMES}


class Rejected(Exception):
    pass


class Evidence:
    def __init__(self, root):
        self.root, self.commands, self.events = root, [], []
        self.root.mkdir(parents=True, exist_ok=True)

    def call(self, argv, label, role="recipe"):
        started = time.perf_counter()
        proc = subprocess.run([str(arg) for arg in argv], capture_output=True, timeout=30)
        index = len(self.commands)
        (self.root / f"{index:02}-{label}.stdout").write_bytes(proc.stdout)
        (self.root / f"{index:02}-{label}.stderr").write_bytes(proc.stderr)
        self.commands.append({"label": label, "role": role, "argv": [str(arg) for arg in argv],
                              "exit": proc.returncode, "seconds": time.perf_counter() - started,
                              "output_bytes": len(proc.stdout) + len(proc.stderr)})
        if proc.returncode:
            raise Rejected(label)

    def event(self, name, **facts):
        self.events.append({"name": name, **facts})


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Origin(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        payload = b"toolx-config-slice-local-origin-v1\n"
        self.send_response(200)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


def proxy_request(port, origin_port):
    with socket.create_connection(("127.0.0.1", port), timeout=0.25) as sock:
        sock.settimeout(0.5)
        sock.sendall((f"GET http://127.0.0.1:{origin_port}/probe HTTP/1.1\r\n"
                      f"Host: 127.0.0.1:{origin_port}\r\nConnection: close\r\n\r\n").encode())
        data = b""
        while True:
            chunk = sock.recv(8192)
            if not chunk:
                break
            data += chunk
        return b"200 OK" in data and data.endswith(b"toolx-config-slice-local-origin-v1\n")


def listener_pids(ports):
    """Read Windows' actual TCP ownership, so an old service cannot pass smoke."""
    api = ctypes.WinDLL("iphlpapi").GetExtendedTcpTable
    size = ctypes.c_ulong(0)
    api(None, ctypes.byref(size), False, 2, 5, 0)
    buffer = ctypes.create_string_buffer(size.value)
    error = api(buffer, ctypes.byref(size), False, 2, 5, 0)
    if error:
        raise OSError(error, "GetExtendedTcpTable")
    rows = struct.unpack_from("<I", buffer)[0]
    result = {}
    for index in range(rows):
        state, _, port_raw, _, _, pid = struct.unpack_from("<6I", buffer, 4 + 24 * index)
        port = socket.ntohs(port_raw & 0xffff)
        if state == 2 and port in ports:
            result[port] = pid
    return result


def smoke(binary, config, evidence, origin_port, role="recipe"):
    """Start the actual application and exercise every expected local inbound."""
    argv = [str(binary), "run", "--disable-color", "-c", str(config)]
    started = time.perf_counter()
    ports = [entry["listen_port"] for entry in json.loads(config.read_text()) ["inbounds"]]
    proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ok, requests, last_error, owners = False, 0, None, {}
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and proc.poll() is None:
            try:
                owners = listener_pids(ports)
                if any(owners.get(port) != proc.pid for port in ports):
                    time.sleep(0.05)
                    continue
                results = []
                for port in ports:
                    requests += 1
                    results.append(proxy_request(port, origin_port))
                if all(results) and proc.poll() is None and all(listener_pids(ports).get(port) == proc.pid for port in ports):
                    ok = True
                    break
            except OSError as exc:
                last_error = str(exc)
            time.sleep(0.05)
    finally:
        natural_exit = proc.poll()
        if natural_exit is None:
            proc.terminate()
        try:
            out, err = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate(timeout=5)
    index = len(evidence.commands)
    (evidence.root / f"{index:02}-smoke.stdout").write_bytes(out)
    (evidence.root / f"{index:02}-smoke.stderr").write_bytes(err)
    evidence.commands.append({"label": "smoke", "role": role, "argv": argv,
        "natural_exit": natural_exit, "exit_after_cleanup": proc.returncode,
        "seconds": time.perf_counter() - started, "output_bytes": len(out) + len(err),
        "proxy_requests": requests, "all_inbounds_responded": ok, "last_socket_error": last_error,
        "child_pid": proc.pid, "listener_pids": owners,
        "owned_listeners_after_cleanup": [port for port, pid in listener_pids(ports).items() if pid == proc.pid]})
    if not ok or evidence.commands[-1]["owned_listeners_after_cleanup"]:
        raise Rejected("startup_or_smoke")


@contextlib.contextmanager
def deny_replace(path, enabled):
    """A real Windows sharing violation, while still allowing independent reads."""
    if not enabled:
        yield
        return
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                                  ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(path), 0x80000000, 0x1 | 0x2, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield
    finally:
        kernel.CloseHandle(handle)


@contextlib.contextmanager
def occupy_inbound(candidate, enabled):
    if not enabled:
        yield
        return
    port = json.loads(candidate.read_text())["inbounds"][0]["listen_port"]
    with socket.socket() as blocker:
        blocker.bind(("127.0.0.1", port))
        blocker.listen()
        yield


def publish_recipe(arm, inputs, current, work, binary, toolx, origin_port, scenario):
    """Straight-line, reusable publication recipe; only composition/policy differ."""
    evidence = Evidence(work / "evidence")
    candidate = work / "candidate.json"
    snapshot = work / "frozen-inputs"
    snapshot.mkdir()
    source_before, target_before = inventory(inputs), sha(current)
    for name in NAMES:
        shutil.copyfile(inputs / name, snapshot / name)
    policy_path = work / "policy.json"
    save(policy_path, POLICY)
    if scenario == "stale_candidate":
        shutil.copyfile(current, candidate)
        evidence.event("seed_old_candidate", sha256=sha(candidate))
    stage, status, failure = "compose", "failed", None
    accepted_sha = None
    started = time.perf_counter()
    try:
        paths = [snapshot / name for name in NAMES]
        if arm == "native":
            evidence.call([binary, "merge", candidate, "--disable-color",
                           *[arg for path in paths for arg in ("-c", path)]], "merge")
            stage = "policy"
            jsonschema.Draft202012Validator(POLICY).validate(json.loads(candidate.read_text()))
            evidence.event("policy", backend="jsonschema", passed=True)
        elif arm == "python":
            from deepmerge import Merger
            merger = Merger([(dict, ["merge"]), (list, ["append"])], ["override"], ["override"])
            composed = {}
            for path in paths:
                merger.merge(composed, json.loads(path.read_text()))
            save(candidate, composed)
            evidence.event("compose", backend="deepmerge", passed=True)
            stage = "policy"
            jsonschema.Draft202012Validator(POLICY).validate(composed)
            evidence.event("policy", backend="jsonschema", passed=True)
        else:
            evidence.call([toolx, "--json", "--base", paths[0],
                           *[arg for path in paths[1:] for arg in ("--overlay", path)],
                           "--append-arrays", "--schema", policy_path, "--out", candidate], "sync")
        accepted_sha = sha(candidate)
        stage = "native_check"
        evidence.call([binary, "check", "--disable-color", "-c", candidate], "check")
        stage = "startup_or_smoke"
        with occupy_inbound(candidate, scenario == "occupied_inbound"):
            if scenario == "occupied_inbound":
                evidence.event("fault", kind="real_listener_owns_expected_port", owner_pid=os.getpid())
            smoke(binary, candidate, evidence, origin_port)
        if scenario == "candidate_changed":
            with candidate.open("ab") as stream:
                stream.write(b"\n ")
            evidence.event("fault", kind="post_validation_candidate_edit", sha256=sha(candidate))
        if scenario == "input_changed":
            path = inputs / NAMES[-1]
            with path.open("ab") as stream:
                stream.write(b"\n ")
            evidence.event("fault", kind="source_input_edit", sha256=sha(path))
        if scenario == "target_changed":
            with current.open("ab") as stream:
                stream.write(b"\n ")
            evidence.event("fault", kind="another_publisher_target_edit", sha256=sha(current))
        stage = "publish_guard"
        if sha(candidate) != accepted_sha:
            raise Rejected("candidate_changed")
        if inventory(inputs) != source_before or inventory(snapshot) != source_before:
            raise Rejected("input_changed")
        if sha(current) != target_before:
            raise Rejected("target_changed")
        if target_before == accepted_sha:
            status = "unchanged"
        else:
            # Same-directory replacement; target is owned exclusively in this
            # experiment. The preceding hash guard is not a cross-process CAS.
            stage = "publish_replace"
            prepared = current.with_suffix(".prepared")
            shutil.copyfile(candidate, prepared)
            if sha(prepared) != accepted_sha:
                raise Rejected("prepared_bytes_changed")
            with deny_replace(current, scenario == "publish_io_failure"):
                os.replace(prepared, current)
            status = "published"
    except (Rejected, jsonschema.ValidationError, OSError) as exc:
        failure = {"type": type(exc).__name__, "message": str(exc), "stage": stage}
    result = {"arm": arm, "scenario": scenario, "status": status, "failure": failure,
        "source_before": source_before, "source_after": inventory(inputs),
        "target_before": target_before, "target_after": sha(current),
        "accepted_candidate_sha256": accepted_sha, "candidate_after": sha(candidate),
        "prepared_after": sha(current.with_suffix(".prepared")),
        "recipe_seconds": time.perf_counter() - started,
        "commands": evidence.commands, "events": evidence.events}
    save(work / "receipt.json", result)
    return result, evidence


def fragments(ports):
    return [{"log": {"level": "warn"},
             "inbounds": [{"type": "mixed", "tag": "generated-in", "listen": "127.0.0.1", "listen_port": ports[0]}],
             "outbounds": [{"type": "direct", "tag": "direct"}]},
            {"inbounds": [{"type": "mixed", "tag": "extra-in", "listen": "127.0.0.1", "listen_port": ports[1]}]},
            {"route": {"final": "direct", "rules": [{"inbound": ["extra-in"], "action": "route", "outbound": "direct"}]}},
            {"log": {"timestamp": False}}]


def oracle(result, current, work, binary, origin_port, evidence, ports, expected_target):
    """Observe files and actual application behavior independently of CLI receipts."""
    success = result["scenario"] in ("normal", "repeat", "repair")
    checks = {"status": (result["status"] != "failed") == success,
              "target_identity": sha(current) == expected_target}
    if success:
        config = json.loads(current.read_text())
        # Native typed output may omit false defaults and compact one-element
        # string lists. Normalize only these known equivalent representations.
        config["log"].setdefault("timestamp", False)
        for rule in config["route"]["rules"]:
            rule.setdefault("action", "route")
            if isinstance(rule.get("inbound"), str):
                rule["inbound"] = [rule["inbound"]]
        expected_config = {"log": {"level": "warn", "timestamp": False},
            "inbounds": [{"type": "mixed", "tag": tag, "listen": "127.0.0.1", "listen_port": port}
                         for tag, port in zip(("generated-in", "extra-in"), ports)],
            "outbounds": [{"type": "direct", "tag": "direct"}],
            "route": {"final": "direct", "rules": [{"inbound": ["extra-in"], "action": "route", "outbound": "direct"}]}}
        checks["shape"] = config == expected_config
        checks["exact_status"] = result["status"] == ("unchanged" if result["scenario"] == "repeat" else "published")
        checks["validated_equals_published"] = (sha(current) == result["accepted_candidate_sha256"]
                                                == sha(work / "candidate.json"))
        try:
            evidence.call([binary, "check", "--disable-color", "-c", current], "oracle-check", "oracle")
            smoke(binary, current, evidence, origin_port, "oracle")
            checks["native_and_proxy"] = True
        except Rejected:
            checks["native_and_proxy"] = False
    else:
        checks["no_false_receipt"] = result["failure"] is not None
        allowed_stages = {"type_failure": {"policy"} if result["arm"] == "python" else {"compose"},
            "policy_failure": {"compose"} if result["arm"] == "toolx" else {"policy"},
            "stale_candidate": {"compose"} if result["arm"] == "toolx" else {"policy"},
            "native_failure": {"native_check"}, "startup_failure": {"startup_or_smoke"}, "occupied_inbound": {"startup_or_smoke"},
            "candidate_changed": {"publish_guard"}, "input_changed": {"publish_guard"},
            "target_changed": {"publish_guard"}, "publish_io_failure": {"publish_replace"}}
        checks["expected_failure_stage"] = result["failure"] is not None and result["failure"]["stage"] in allowed_stages[result["scenario"]]
        if result["scenario"] in ("candidate_changed", "input_changed", "target_changed"):
            checks["fault_triggered"] = any(event["name"] == "fault" for event in result["events"])
            checks["exact_rejection"] = result["failure"] is not None and result["failure"]["message"] == result["scenario"]
        if result["scenario"] == "publish_io_failure":
            checks["partial_retained"] = result["prepared_after"] is not None and result["candidate_after"] is not None
            checks["real_sharing_violation"] = result["failure"] is not None and result["failure"]["type"] == "PermissionError"
            checks["partial_identity"] = result["prepared_after"] == result["candidate_after"] == result["accepted_candidate_sha256"]
    report = {"passed": all(checks.values()), "checks": checks}
    save(work / "oracle.json", report)
    save(work / "all-commands.json", evidence.commands)
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--sing-box", type=Path, required=True)
    parser.add_argument("--toolx", type=Path, required=True)
    parser.add_argument("--python-deps", type=Path, required=True)
    args = parser.parse_args()
    root, binary, toolx = args.root.resolve(), args.sing_box.resolve(), args.toolx.resolve()
    sys.path.insert(0, str(args.python_deps.resolve()))
    root.mkdir(parents=True, exist_ok=False)
    save(root / "predeclared.json", {"task": "podkop/sing-box additional inbound and route publication boundary",
        "task_source": "https://github.com/itdoginfo/podkop/issues/280", "scenarios": SCENARIOS,
        "equal_facts": {"input_order": NAMES, "merge": "append arrays; no conflicting scalar fields in this task",
            "policy": POLICY, "native_check_required": True, "all_local_inbounds_must_proxy": True,
            "publish": "same shared frozen-input/hash guards/atomic replacement; preserve current on failure; skip identical bytes"},
        "excluded": ["runner", "OpenWrt/UCI", "router deployment", "reload/zero downtime", "cross-process CAS", "crash durability", "model-token experiment"]})
    save(root / "environment.json", {"python": sys.version, "platform": platform.platform(),
         "jsonschema": __import__("importlib.metadata", fromlist=["version"]).version("jsonschema"),
         "deepmerge": __import__("importlib.metadata", fromlist=["version"]).version("deepmerge"),
         "toolx": str(toolx), "toolx_sha256": sha(toolx), "sing_box": str(binary), "sing_box_sha256": sha(binary)})
    origin = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Origin)
    thread = threading.Thread(target=origin.serve_forever, daemon=True)
    thread.start()
    rows = []
    try:
        ports = [unused_port(), unused_port()]
        while ports[0] == ports[1]:
            ports[1] = unused_port()
        for arm in ("native", "python", "toolx"):
            home = root / arm
            inputs, current = home / "inputs", home / "current.json"
            inputs.mkdir(parents=True)
            good = fragments(ports)
            # A pre-existing valid generation, visibly different from desired.
            seed = {"log": {"disabled": True}, "outbounds": [{"type": "direct", "tag": "direct"}]}
            save(current, seed)
            normal_sha = None
            for scenario in SCENARIOS:
                for name, data in zip(NAMES, good):
                    save(inputs / name, data)
                repair_commands = 0
                if scenario == "repair":
                    # Reproduce the actual failed input, locate it, then edit
                    # that file. Do not quietly regenerate all-good fragments.
                    for name in NAMES:
                        shutil.copyfile(home / "startup_failure" / "frozen-inputs" / name, inputs / name)
                    diagnostic = Evidence(home / scenario / "diagnosis")
                    diagnostic.call(["rg", "-n", "--fixed-strings", "missing-outbound", inputs], "locate", "repair")
                    path = inputs / NAMES[2]
                    bad_sha = sha(path)
                    fixed = json.loads(path.read_text())
                    fixed["route"]["final"] = "direct"
                    save(path, fixed)
                    save(home / scenario / "repair.json", {"commands": diagnostic.commands,
                         "files_edited": [NAMES[2]], "before_sha256": bad_sha, "after_sha256": sha(path),
                         "reused": "same recipe, native binary, ToolX binary, project facts and port assignments"})
                    repair_commands = len(diagnostic.commands)
                if scenario == "type_failure":
                    bad = json.loads((inputs / NAMES[1]).read_text())
                    bad["inbounds"][0]["listen_port"] = "not-a-port"
                    save(inputs / NAMES[1], bad)
                if scenario in ("policy_failure", "stale_candidate"):
                    bad = json.loads((inputs / NAMES[1]).read_text())
                    bad["inbounds"][0]["listen"] = "0.0.0.0"
                    save(inputs / NAMES[1], bad)
                if scenario == "native_failure":
                    bad = json.loads((inputs / NAMES[2]).read_text())
                    bad["route"]["rules"].append({"domain_regex": ["["], "action": "route", "outbound": "direct"})
                    save(inputs / NAMES[2], bad)
                if scenario == "startup_failure":
                    bad = json.loads((inputs / NAMES[2]).read_text())
                    bad["route"]["final"] = "missing-outbound"
                    save(inputs / NAMES[2], bad)
                if scenario == "publish_io_failure":
                    # Make this a genuine attempted replacement, not no-op.
                    save(current, seed)
                before_bytes = current.read_bytes()
                before = sha(current)
                result, evidence = publish_recipe(arm, inputs, current, home / scenario,
                                                  binary, toolx, origin.server_port, scenario)
                if scenario == "normal":
                    normal_sha = result["accepted_candidate_sha256"]
                expected = normal_sha if scenario in ("normal", "repeat", "repair") else before
                if scenario == "target_changed":
                    # Exact bytes of the injected competing publisher, computed
                    # independently of the recipe's target-after receipt.
                    expected = hashlib.sha256(before_bytes + b"\n ").hexdigest()
                check = oracle(result, current, home / scenario, binary, origin.server_port, evidence, ports, expected)
                rows.append({**{key: result[key] for key in ("arm", "scenario", "status", "failure", "recipe_seconds")},
                    "recipe_commands": sum(cmd["role"] == "recipe" for cmd in evidence.commands),
                    "oracle_commands": sum(cmd["role"] == "oracle" for cmd in evidence.commands),
                    "recipe_output_bytes": sum(cmd["output_bytes"] for cmd in evidence.commands if cmd["role"] == "recipe"),
                    "repair_commands": repair_commands,
                    "oracle_passed": check["passed"]})
                print(json.dumps(rows[-1]), flush=True)
    finally:
        origin.shutdown()
        origin.server_close()
        thread.join(timeout=5)
    save(root / "summary.json", rows)
    with zipfile.ZipFile(root / "evidence.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.name != "evidence.zip":
                archive.write(path, path.relative_to(root))
        for path in sorted(Path(__file__).parent.glob("*.py")):
            archive.write(path, "experiment-code/" + path.name)
    if not all(row["oracle_passed"] for row in rows):
        raise SystemExit("independent oracle failed")


if __name__ == "__main__":
    main()
