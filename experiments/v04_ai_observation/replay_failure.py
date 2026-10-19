"""Replay the observed publication fault against a fixed binary, not a model trial."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--toolx", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    home = args.observation.resolve() / "available"
    candidate = home / "logs/publish-failure-real/candidate.json"
    current = out / "current.json"
    shutil.copyfile(home / "release/current.json", current)
    before = digest(current)
    blocker = out / "blocker"
    blocker.write_text("not a directory", encoding="utf-8")
    argv = [str(args.toolx.resolve()), "--json", "--base", str(candidate), "--out", str(current),
            "--snapshot", str(blocker / "child.json"), "--journal", str(out / "publish.journal")]
    proc = subprocess.run(argv, capture_output=True, timeout=30)
    (out / "stdout.json").write_bytes(proc.stdout)
    (out / "stderr.txt").write_bytes(proc.stderr)
    data = json.loads(proc.stdout)["data"]
    checks = {"actual_failure": proc.returncode == 1, "different_candidate": digest(candidate) != before,
        "old_target_preserved": digest(current) == before,
        "publish_attempted": data["publish_attempted"] is True,
        "forward_step_recorded": data["execution"]["completed_steps"] == 1,
        "rollback_complete": data["execution"]["rollback_complete"] is True,
        "restored_step_recorded": data["execution"]["steps"][0]["rolled_back"] is True}
    record = {"kind": "deterministic contract replay; not AI cost measurement", "argv": argv,
              "exit": proc.returncode, "binary_sha256": digest(args.toolx), "checks": checks,
              "passed": all(checks.values())}
    (out / "result.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps(record, indent=2))
    if not record["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
