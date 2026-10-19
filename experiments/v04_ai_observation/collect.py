"""Archive task inputs, agent artifacts and independent acceptance evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    files = [path for path in root.rglob("*") if path.is_file()
             and path.suffix != ".zip" and path != root / "manifest.json" and "__pycache__" not in path.parts]
    records = [{"path": path.relative_to(root).as_posix(), "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in sorted(files)]
    manifest = root / "manifest.json"
    manifest.write_text(json.dumps(records, indent=2), encoding="utf-8")
    output = root / "ai-observation.zip"
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, path.relative_to(root))
        archive.write(manifest, "manifest.json")
        for path in Path(__file__).parent.glob("*.py"):
            archive.write(path, "observation-code/" + path.name)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        members = len(archive.namelist())
    print(json.dumps({"path": str(output), "members": members,
                      "bytes": output.stat().st_size, "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
