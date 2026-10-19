"""Record native sing-box and existing ToolX merge semantics without assertions.

The results are observations of the supplied binaries, not a generic merge spec.
Only scratch files under --out are written. No network or running service is used.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def execute(argv: list[str], cwd: Path, output: Path | None = None) -> dict:
    start = time.perf_counter()
    result = subprocess.run(argv, cwd=cwd, capture_output=True, timeout=30)
    record = {
        "argv": argv,
        "cwd": str(cwd),
        "exit": result.returncode,
        "stdout": result.stdout.decode("utf-8", errors="replace"),
        "stderr": result.stderr.decode("utf-8", errors="replace"),
        "elapsed_ms": round((time.perf_counter() - start) * 1000, 3),
    }
    if output is not None:
        record["output_path"] = str(output)
        record["output_exists"] = output.exists()
        if output.exists():
            raw = output.read_bytes()
            record["output_sha256"] = hashlib.sha256(raw).hexdigest()
            record["output_text"] = raw.decode("utf-8", errors="replace")
            try:
                record["output_json"] = json.loads(raw)
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                record["output_parse_error"] = str(exc)
    return record


def projected(output: object) -> object:
    if not isinstance(output, dict):
        return output
    return {key: output[key] for key in ("log", "inbounds", "route") if key in output}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--native", type=Path, default=root / "temp/v04-config-20261018/deps/sing-box.exe")
    parser.add_argument("--toolx", type=Path, required=True,
                        help="Explicit binary path; rebuild the selected revision before using results as current-source evidence")
    parser.add_argument("--out", type=Path, default=root / "temp/v04-config-20261018/probes")
    args = parser.parse_args()
    native, toolx, out = args.native.resolve(), args.toolx.resolve(), args.out.resolve()
    if (out / "probes.json").exists():
        parser.error("--out already contains probes.json; choose a new path to preserve evidence")
    out.mkdir(parents=True, exist_ok=True)

    mixed_a = {"type": "mixed", "tag": "a", "listen": "127.0.0.1", "listen_port": 31001}
    mixed_b = {"type": "mixed", "tag": "b", "listen": "127.0.0.1", "listen_port": 31002}
    mixed_same = {**mixed_b, "tag": "a"}
    cases = [
        ("scalar_conflict", {"log": {"level": "info"}}, {"log": {"level": "debug"}}),
        ("arrays_append", {"inbounds": [mixed_a]}, {"inbounds": [mixed_b]}),
        ("empty_array_clear", {"inbounds": [mixed_a]}, {"inbounds": []}),
        ("null_overlay", {"log": {"level": "info"}}, {"log": None}),
        ("null_base", {"log": None}, {"log": {"level": "debug"}}),
        ("type_conflict_overlay", {"log": {"level": "info"}}, {"log": "invalid-log-type"}),
        ("type_conflict_base", {"log": "invalid-log-type"}, {"log": {"level": "debug"}}),
        ("duplicate_tags", {"inbounds": [mixed_a]}, {"inbounds": [mixed_same]}),
        ("nested_array_append", {"route": {"rules": [{"domain": ["one.example"], "outbound": "direct"}]}},
         {"route": {"rules": [{"domain": ["two.example"], "outbound": "direct"}]}}),
        ("scalar_zero_value", {"log": {"disabled": True}}, {"log": {"disabled": False}}),
    ]
    metadata = {
        "native_binary": str(native),
        "native_sha256": hashlib.sha256(native.read_bytes()).hexdigest(),
        "toolx_binary": str(toolx),
        "toolx_sha256": hashlib.sha256(toolx.read_bytes()).hexdigest(),
        "native_version": execute([str(native), "version"], out),
        "native_merge_help": execute([str(native), "merge", "--help"], out),
        "toolx_help": execute([str(toolx), "--help"], out),
        "notes": [
            "No separate native check is run; merge's built-in typed decoding and duplicate checks remain enabled.",
            "Native may normalize or omit fields; output JSON and exact bytes are both recorded.",
            "ToolX append mode is paired with native; default replace is additionally observed.",
            "Reverse native -c order retains filenames to distinguish argument order from filename sorting.",
        ],
    }
    records = []
    summaries = []
    for name, base, overlay in cases:
        case_dir = out / name
        case_dir.mkdir(parents=True, exist_ok=True)
        base_path, overlay_path = case_dir / "10-base.json", case_dir / "20-overlay.json"
        write_json(base_path, base)
        write_json(overlay_path, overlay)
        runs = []
        for order, paths in (("forward", (base_path, overlay_path)), ("reverse", (overlay_path, base_path))):
            output = case_dir / ("native-" + order + ".json")
            record = execute([str(native), "merge", str(output), "-c", str(paths[0]), "-c", str(paths[1]), "--disable-color"], case_dir, output)
            record.update({"case": name, "backend": "native", "mode": order})
            runs.append(record)
            for append in (True, False):
                mode = order + ("-append" if append else "-replace")
                output = case_dir / ("toolx-" + mode + ".json")
                argv = [str(toolx), "--json", "--base", str(paths[0]), "--overlay", str(paths[1]), "--out", str(output)]
                if append:
                    argv.append("--append-arrays")
                record = execute(argv, case_dir, output)
                record.update({"case": name, "backend": "toolx", "mode": mode})
                runs.append(record)
        # Native scalar tie-breaking also needs filenames reversed without changing -c declaration order.
        if name == "scalar_conflict":
            renamed_base, renamed_overlay = case_dir / "90-base.json", case_dir / "01-overlay.json"
            write_json(renamed_base, base)
            write_json(renamed_overlay, overlay)
            output = case_dir / "native-renamed.json"
            record = execute([str(native), "merge", str(output), "-c", str(renamed_base), "-c", str(renamed_overlay), "--disable-color"], case_dir, output)
            record.update({"case": name, "backend": "native", "mode": "renamed"})
            runs.append(record)
        for record in runs:
            records.append(record)
            summaries.append({
                "case": name, "backend": record["backend"], "mode": record["mode"],
                "exit": record["exit"], "output_exists": record["output_exists"],
                "result": projected(record.get("output_json")),
                "stderr": record["stderr"],
            })
    write_json(out / "probes.json", {"metadata": metadata, "runs": records})
    write_json(out / "summary.json", summaries)
    print(json.dumps({"out": str(out), "runs": len(records), "exit_counts": {
        str(code): sum(r["exit"] == code for r in records) for code in sorted({r["exit"] for r in records})
    }}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
