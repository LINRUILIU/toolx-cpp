"""Independent artifact/runtime and common negative checks, never an agent recipe."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.request


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def listener_pids(ports):
    # Same Windows ownership primitive as the earlier independent oracle.
    api = ctypes.WinDLL("iphlpapi").GetExtendedTcpTable
    size = ctypes.c_ulong(0)
    api(None, ctypes.byref(size), False, 2, 5, 0)
    buffer = ctypes.create_string_buffer(size.value)
    error = api(buffer, ctypes.byref(size), False, 2, 5, 0)
    if error:
        raise OSError(error, "GetExtendedTcpTable")
    result = {}
    for index in range(struct.unpack_from("<I", buffer)[0]):
        state, _, port_raw, _, _, pid = struct.unpack_from("<6I", buffer, 4 + 24 * index)
        port = socket.ntohs(port_raw & 0xffff)
        if state == 2 and port in ports:
            result[port] = pid
    return result


def native_accept(home, config, binary, output):
    output.mkdir(parents=True, exist_ok=True)
    commands = []
    check = subprocess.run([str(binary), "validate", "--config", str(config)],
                           capture_output=True, timeout=30)
    (output / "validate.stdout").write_bytes(check.stdout)
    (output / "validate.stderr").write_bytes(check.stderr)
    commands.append({"argv": [str(binary), "validate", "--config", str(config)], "exit": check.returncode})
    if check.returncode:
        return {"passed": False, "commands": commands, "reason": "native validation failed"}
    ports = {61480, 61481, 61482}
    if listener_pids(ports):
        raise RuntimeError("allocated ports already owned; cannot credit a previous service")
    env = os.environ.copy()
    for key in ("APPDATA", "LOCALAPPDATA", "XDG_DATA_HOME", "XDG_CONFIG_HOME"):
        env[key] = str(output / "private-app-data")
    origin_log = (output / "origin.stderr").open("wb")
    caddy_log = (output / "caddy.stderr").open("wb")
    origin = subprocess.Popen([sys.executable, str(home / "origin.py")], cwd=home,
                              stdout=subprocess.DEVNULL, stderr=origin_log, env=env)
    caddy = subprocess.Popen([str(binary), "run", "--config", str(config)], cwd=home,
                             stdout=subprocess.DEVNULL, stderr=caddy_log, env=env)
    responses, owners, error = [], {}, ""
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            owners = listener_pids(ports)
            if owners == {61480: caddy.pid, 61481: caddy.pid, 61482: origin.pid}:
                break
            if caddy.poll() is not None or origin.poll() is not None:
                raise RuntimeError("owned application exited before readiness")
            time.sleep(0.05)
        if owners != {61480: caddy.pid, 61481: caddy.pid, 61482: origin.pid}:
            raise RuntimeError("listener ownership mismatch")
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        cases = [("app.test", "/api/ping", "api-v2:/api/ping"),
                 ("app.test", "/", "spa-v2"), ("app.test", "/app/deep", "spa-v2"),
                 ("health.test", "/", "ready-v2")]
        for host, path, expected in cases:
            request = urllib.request.Request("http://127.0.0.1:61480" + path, headers={"Host": host})
            with opener.open(request, timeout=5) as response:
                body = response.read().decode()
                responses.append({"host": host, "path": path, "status": response.status,
                                  "body": body, "passed": response.status == 200 and body == expected})
    except Exception as exc:
        error = str(exc)
    finally:
        for process in (caddy, origin):
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        origin_log.close()
        caddy_log.close()
    remaining = listener_pids(ports)
    origin_received_api = b'"GET /api/ping HTTP/1.1" 200' in (output / "origin.stderr").read_bytes()
    row = {"commands": commands, "owners": owners, "requests": responses, "error": error,
           "owned_origin_received_api_ping": origin_received_api,
           "owned_listener_cleanup": not remaining,
           "passed": not error and len(responses) == 4 and all(x["passed"] for x in responses)
                     and origin_received_api and not remaining}
    save(output / "runtime.json", row)
    return row


def file_state(paths):
    return {str(path): path.read_bytes() if path.is_file() else None for path in paths}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_receipt(stdout):
    """Receipts corroborate raw artifacts; an exit code alone proves no stage."""
    lines = stdout.decode("utf-8", errors="replace").strip().splitlines()
    try:
        value = json.loads(lines[-1])
    except (IndexError, ValueError):
        return None
    if not isinstance(value, dict):
        return None
    required = {"ok", "stage", "candidate_sha256", "publish_attempted", "error"}
    if not required.issubset(value):
        return None
    digest = value["candidate_sha256"]
    if (type(value["ok"]) is not bool or type(value["publish_attempted"]) is not bool
            or not isinstance(value["stage"], str)
            or value["stage"] not in {"compose", "validation", "acceptance", "publish", "complete"}
            or not isinstance(value["error"], str)
            or (digest is not None and not (isinstance(digest, str)
                    and re.fullmatch("[0-9a-f]{64}", digest)))):
        return None
    return value


def invoke(home, inputs, outputs, evidence):
    evidence.mkdir(parents=True, exist_ok=False)
    recipe_evidence = evidence / "recipe-evidence"
    recipe_evidence.mkdir()
    argv = [sys.executable, str(home / "publish.py"), "--inputs-dir", str(inputs),
            "--current", str(outputs[0]), "--accepted", str(outputs[1])]
    env = os.environ.copy()
    env["TOOLX_OBSERVATION_EVIDENCE"] = str(recipe_evidence)
    try:
        call = subprocess.run(argv, cwd=home, capture_output=True, timeout=90, env=env)
        stdout, stderr, exit_code = call.stdout, call.stderr, call.returncode
    except subprocess.TimeoutExpired as exc:
        stdout, stderr, exit_code = exc.stdout or b"", exc.stderr or b"", None
    (evidence / "stdout.txt").write_bytes(stdout)
    (evidence / "stderr.txt").write_bytes(stderr)
    receipt = read_receipt(stdout)
    row = {"argv": argv, "exit": exit_code, "receipt": receipt,
           "recipe_evidence": str(recipe_evidence)}
    save(evidence / "invocation.json", row)
    return row


def retained_candidates(invocation):
    receipt = invocation["receipt"]
    if not receipt or not receipt["candidate_sha256"]:
        return []
    result = []
    for path in Path(invocation["recipe_evidence"]).rglob("*"):
        if not path.is_file() or path.stat().st_size > 10 * 1024 * 1024:
            continue
        data = path.read_bytes()
        if sha256(data) == receipt["candidate_sha256"]:
            try:
                if isinstance(json.loads(data), dict):
                    result.append(path)
            except ValueError:
                pass
    return result


def raw_diagnostics(invocation, evidence):
    parts = [invocation["receipt"]["error"] if invocation["receipt"] else ""]
    for path in [evidence / "stdout.txt", evidence / "stderr.txt", *Path(invocation["recipe_evidence"]).rglob("*")]:
        if path.is_file() and path.stat().st_size <= 10 * 1024 * 1024:
            parts.append(path.read_bytes().decode("utf-8", errors="replace"))
    return "\n".join(parts)


def has_os_publication_diagnostic(diagnostics):
    # Win32 APIs can report access denied (5), sharing violation (32), or
    # lock violation (33) for this held handle. std::error_code may omit the
    # numeric code and use English while ctypes uses the Windows UI language.
    os_messages = [ctypes.FormatError(code).strip().rstrip(".").casefold()
                   for code in (5, 32, 33)]
    return bool(re.search(r"(?:WinError|winerror|error(?:_code)?)[\s\"':=\[]+(?:5|32|33)\b", diagnostics, re.I)
                or any(message and message in diagnostics.casefold() for message in os_messages)
                or "being used by another process" in diagnostics.casefold()
                or "access is denied" in diagnostics.casefold()
                or "另一个程序正在使用此文件" in diagnostics)


def compose_expected(inputs):
    # Deliberately independent standard-library oracle, never given to agents.
    def merge(base, overlay):
        if isinstance(base, dict) and isinstance(overlay, dict):
            result = dict(base)
            for key, value in overlay.items():
                result[key] = merge(result[key], value) if key in result else value
            return result
        return overlay
    result = {}
    for path in sorted(inputs.glob("*.json")):
        result = merge(result, json.loads(path.read_bytes()))
    return result


def positive_control(home, inputs, outputs, binary, evidence):
    before = file_state(outputs)
    invocation = invoke(home, inputs, outputs, evidence / "call")
    after = file_state(outputs)
    receipt = invocation["receipt"]
    candidates = retained_candidates(invocation)
    data = after[str(outputs[0])]
    shape_matches = False
    if data is not None:
        try:
            shape_matches = json.loads(data) == compose_expected(inputs)
        except ValueError:
            pass
    native = native_accept(home, outputs[0], binary, evidence / "native") if data is not None else {"passed": False}
    checks = {
        "successful_receipt": bool(receipt and receipt["ok"] and receipt["stage"] == "complete"
                                   and receipt["publish_attempted"] and not receipt["error"]),
        "both_outputs_identical": data is not None and data == after[str(outputs[1])],
        "different_from_previous": before != after,
        "all_input_values_composed": shape_matches,
        "exact_candidate_retained": bool(receipt and candidates and data is not None
                                         and receipt["candidate_sha256"] == sha256(data)),
        "native_acceptance": native["passed"],
    }
    row = {"invocation": invocation, "checks": checks, "native": native,
           "candidate_sha256": sha256(data) if data is not None else None,
           "passed": invocation["exit"] == 0 and all(checks.values())}
    save(evidence / "result.json", row)
    return row


def lock_output(path, evidence):
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                               wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    # Allow reads only: deny both in-place writes and delete/rename replacement.
    handle = api.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x80, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    probe = path.with_name(path.name + ".observer-lock-probe")
    probe.write_bytes(b"independent lock calibration, never an accepted candidate")
    try:
        os.replace(probe, path)
        row = {"blocked": False, "winerror": None, "error": "replacement unexpectedly succeeded"}
    except OSError as exc:
        row = {"blocked": True, "winerror": exc.winerror, "error": str(exc)}
    finally:
        probe.unlink(missing_ok=True)
    save(evidence, row)
    return api, handle, row


def test_negatives(home, evidence, binary):
    # Positive controls reject the trivial "always fails" script and prove the
    # exact different candidate is accepted before a publication lock is held.
    # All controls/faults are outside agent directories until initial attempts.
    rows, controls = [], []
    outputs = [home / "release/current.json", home / "release/accepted.json"]
    original = file_state(outputs)
    source = home / "inputs"
    alternate = evidence / "alternate-valid" / "inputs"
    shutil.copytree(source, alternate)
    path = alternate / "10-environment.json"
    value = json.loads(path.read_text())
    value["logging"]["logs"]["default"]["level"] = "WARN"
    save(path, value)
    alternate_outputs = [alternate.parent / "release/current.json", alternate.parent / "release/accepted.json"]
    for index, path in enumerate(alternate_outputs):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(original[str(outputs[index])])
    controls.append(positive_control(home, alternate, alternate_outputs, binary, alternate.parent / "control"))

    debug_inputs = evidence / "debug-unlocked" / "inputs"
    shutil.copytree(source, debug_inputs)
    path = debug_inputs / "10-environment.json"
    value = json.loads(path.read_text())
    value["logging"]["logs"]["default"]["level"] = "DEBUG"
    save(path, value)
    debug_outputs = [debug_inputs.parent / "release/current.json", debug_inputs.parent / "release/accepted.json"]
    for index, path in enumerate(debug_outputs):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(original[str(outputs[index])])
    debug = positive_control(home, debug_inputs, debug_outputs, binary, debug_inputs.parent / "control")
    controls.append(debug)

    for name in ("native-invalid", "stale-compose", "locked-second-output"):
        inputs = evidence / name / "inputs"
        shutil.copytree(debug_inputs if name == "locked-second-output" else source, inputs)
        if name == "native-invalid":
            path = inputs / "20-sites.json"
            value = json.loads(path.read_text())
            value["apps"]["http"]["servers"]["local"]["routes"][0]["handle"][0]["handler"] = "unavailable_test_handler"
            save(path, value)
        elif name == "stale-compose":
            (inputs / "20-sites.json").write_text('{"apps":', encoding="utf-8")
        api, handle, lock = None, None, None
        if name == "locked-second-output":
            api, handle, lock = lock_output(outputs[1], inputs.parent / "lock-calibration.json")
        try:
            invocation = invoke(home, inputs, outputs, inputs.parent / "call")
        finally:
            if handle is not None:
                api.CloseHandle(handle)
        after = file_state(outputs)
        receipt = invocation["receipt"]
        candidates = retained_candidates(invocation)
        diagnostics = raw_diagnostics(invocation, inputs.parent / "call")
        corroborated = False
        details = {}
        if receipt and not receipt["ok"] and receipt["error"]:
            if name == "locked-second-output":
                # The independent replacement probe establishes the actual
                # OS failure; do not assume one API's code for another API.
                os_diagnostic = has_os_publication_diagnostic(diagnostics)
                corroborated = bool(debug["passed"] and lock["blocked"] and lock["winerror"] in {5, 32, 33}
                                    and receipt["stage"] == "publish" and receipt["publish_attempted"]
                                    and candidates and receipt["candidate_sha256"] == debug["candidate_sha256"]
                                    and receipt["candidate_sha256"] != sha256(original[str(outputs[0])])
                                    and os_diagnostic)
                details = {"lock": lock, "exact_unlocked_candidate_sha256": debug["candidate_sha256"],
                           "raw_os_publication_diagnostic": os_diagnostic}
            elif name == "native-invalid":
                invalid = None
                if candidates:
                    call = subprocess.run([str(binary), "validate", "--config", str(candidates[0])],
                                          capture_output=True, timeout=30)
                    (inputs.parent / "independent-native.stdout").write_bytes(call.stdout)
                    (inputs.parent / "independent-native.stderr").write_bytes(call.stderr)
                    invalid = call.returncode
                corroborated = bool(receipt["stage"] == "validation" and not receipt["publish_attempted"]
                                    and candidates and invalid not in (None, 0)
                                    and "unavailable_test_handler" in diagnostics)
                details = {"independent_native_exit": invalid}
            else:
                corroborated = (receipt["stage"] == "compose" and not receipt["publish_attempted"]
                                and receipt["candidate_sha256"] is None)
        failed = invocation["exit"] not in (None, 0)
        preserved = original == after
        passed = failed and preserved and corroborated
        row = {"name": name, "invocation": invocation,
               "both_previous_files_preserved": original == after,
               "intended_failure_corroborated": corroborated, "details": details,
               "outcome": "passed" if passed else "inconclusive" if failed and preserved else "failed",
               "passed": passed}
        save(inputs.parent / "result.json", row)
        rows.append(row)
        if original != after:
            # Keep the broken result, then restore only our oracle fixture so
            # subsequent faults are independent; never credit that as agent repair.
            for path, data in after.items():
                if data is not None:
                    (inputs.parent / (Path(path).name + ".after")).write_bytes(data)
            for path in outputs:
                if original[str(path)] is not None:
                    path.write_bytes(original[str(path)])
    result = {"positive_controls": controls, "negative_cases": rows,
              "passed": all(row["passed"] for row in controls + rows)}
    save(evidence / "result.json", result)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--arm", choices=["mature", "fixed", "fixed_clean", "released"], required=True)
    parser.add_argument("--label", default="initial")
    args = parser.parse_args()
    root = args.root.resolve()
    home = root / args.arm
    output = root / "independent-oracle" / args.arm / args.label
    output.mkdir(parents=True, exist_ok=False)
    brief = json.loads((home / "brief.json").read_text())
    current, accepted = home / "release/current.json", home / "release/accepted.json"
    value = json.loads(current.read_text())
    ids = []
    def walk(node):
        if isinstance(node, dict):
            if "@id" in node:
                ids.append(node["@id"])
            for child in node.values():
                walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)
    walk(value)
    originals = json.loads((root / "predeclared.json").read_text())["input_sha256"]
    checks = {"identical_release_bytes": current.read_bytes() == accepted.read_bytes(),
              "required_ids_once": sorted(ids) == sorted(["app_site", "app_api", "app_spa", "health_site"]),
              "generated_unchanged": sha256((home / "inputs/00-generated.json").read_bytes()) == originals["00-generated.json"],
              "environment_unchanged": sha256((home / "inputs/10-environment.json").read_bytes()) == originals["10-environment.json"],
              "all_input_values_composed": value == compose_expected(home / "inputs")}
    native = native_accept(home, current, Path(brief["tools"]["caddy"]), output / "native")
    negatives = test_negatives(home, output / "negative", Path(brief["tools"]["caddy"]))
    checks["immutable_inputs_preserved_after_observer_calls"] = all(
        sha256((home / "inputs" / name).read_bytes()) == originals[name]
        for name in ("00-generated.json", "10-environment.json"))
    result = {"arm": args.arm, "label": args.label, "checks": checks, "native": native,
              "negatives": negatives, "current_sha256": hashlib.sha256(current.read_bytes()).hexdigest()}
    result["passed"] = all(checks.values()) and native["passed"] and negatives["passed"]
    save(output / "result.json", result)
    print(json.dumps(result, indent=2))
    if not result["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
