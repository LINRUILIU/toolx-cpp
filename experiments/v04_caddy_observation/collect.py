"""Archive actual attempts, common acceptance evidence and frozen identities."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    repo = Path(__file__).resolve().parents[2]
    gates = repo / "temp/v041-gates"
    selected = ["summary.json", "README.md", "gcc-tests-final.xml", "sanitizer-tests-final.xml",
                "clang-tests-final.xml", "clean-source-content.json", "clean-source-manifest.json",
                "lint-preliminary.log", "lint-final-affected.log", "lint-argtool-final.log"]
    selected += [name + suffix for name in ("gcc-tests-final", "sanitizer-native-test", "format-frozen",
                 "lint-final-affected", "lint-argtool-final", "fuzz-cfgx", "fuzz-httpx", "fuzz-fsx",
                 "clang-native-build", "clang-native-test", "linux-clean-source-archive",
                 "linux-clean-source-check", "source-cpack-fixture")
                 for suffix in (".json", ".log")]
    for name in set(selected):
        path = gates / name
        if path.is_file():
            target = root / "gate-evidence" / name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(path, target)
    for path, target in (
        (repo / "temp/v040-build/verification-closed-windows.xml", root / "gate-evidence/windows.xml"),
        (repo / "temp/v040-build/verification-observation-docs.xml", root / "gate-evidence/final-docs.xml"),
        (repo / "temp/v041-caddy-deps/upstream.json", root / "caddy-upstream.json"),
        (repo / "docs/development/audits/v0.4-caddy-ai-observation.md", root / "audit.md"),
    ):
        shutil.copyfile(path, target)
    for path in Path(__file__).parent.iterdir():
        if path.is_file() and path.suffix in {".py", ".md"}:
            target = root / "observation-code" / path.name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(path, target)
    # Preserve unsuccessful initial attempts and repairs, rather than choosing
    # only successful outputs for the archive.
    verdicts = {}
    for arm in ("mature", "fixed", "fixed_clean", "released"):
        paths = sorted((root / "independent-oracle" / arm).glob("*/result.json"), key=lambda p: p.stat().st_mtime_ns)
        if not paths or not (root / arm / "observation.json").is_file():
            raise ValueError("incomplete observation: " + arm)
        verdicts[arm] = [{"path": path.relative_to(root).as_posix(),
                          "passed": json.loads(path.read_text())["passed"]} for path in paths]
    (root / "verdicts.json").write_text(json.dumps(verdicts, indent=2), encoding="utf-8")
    files = sorted(path for path in root.rglob("*") if path.is_file() and not path.is_symlink()
                   and path.suffix not in {".zip", ".pyc", ".pyo"}
                   and path.name != "manifest.json" and "__pycache__" not in path.parts)
    records = [{"path": path.relative_to(root).as_posix(), "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in files]
    manifest = root / "manifest.json"
    manifest.write_text(json.dumps(records, indent=2), encoding="utf-8")
    output = root / "caddy-observation.zip"
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in [*files, manifest]:
            archive.write(path, path.relative_to(root))
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        for record in records:
            data = archive.read(record["path"])
            if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
                raise ValueError("archive manifest mismatch: " + record["path"])
        members = len(archive.namelist())
    print(json.dumps({"path": str(output), "members": members, "bytes": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "verdicts": verdicts}))


if __name__ == "__main__":
    main()
