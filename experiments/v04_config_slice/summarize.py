"""Collect experiment/probe evidence without changing any case results."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--contracts", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    root, results = args.root.resolve(), args.results.resolve()
    rows = json.loads((results / "summary.json").read_text())
    with (root / "metrics.csv").open("w", newline="", encoding="utf-8") as stream:
        fields = [key for key in rows[0] if key != "failure"] + ["failure_stage", "failure_type"]
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            failure = row.pop("failure") or {}
            writer.writerow({**row, "failure_stage": failure.get("stage"), "failure_type": failure.get("type")})
    files = [(path, "results/" + path.relative_to(results).as_posix()) for path in results.rglob("*")
             if path.is_file() and path.name != "evidence.zip"]
    for source, prefix in ((root / "probes/head", "merge-probes"),
                           (args.contracts.resolve(), "contract-probes"),
                           (Path(__file__).resolve().parent, "experiment-code")):
        files += [(path, prefix + "/" + path.relative_to(source).as_posix()) for path in source.rglob("*")
                  if path.is_file() and "__pycache__" not in path.parts]
    for name in ("upstream.json", "upstream-LICENSE.txt", "cmd_run.go", "cmd_merge.go", "cmd.go"):
        path = root / "deps" / name
        if path.is_file():
            files.append((path, "upstream/" + name))
    for name in ("build-record.json", "build.log", "CMakeCache.txt"):
        path = args.build / name
        files.append((path, "toolx-build/" + name))
    files.append((root / "metrics.csv", "metrics.csv"))
    manifest = [{"path": name, "bytes": path.stat().st_size,
                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path, name in sorted(files)]
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    output = root / "config-evidence.zip"
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, name in files:
            archive.write(path, name)
        archive.write(root / "manifest.json", "manifest.json")
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        members = len(archive.namelist())
    print(json.dumps({"archive": str(output), "members": members, "bytes": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
