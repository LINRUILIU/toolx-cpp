"""Cache a pinned upstream Caddy binary; never install system-wide."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

VERSION = "2.10.2"
ARCHIVE_SHA256 = "9fd1ef9be5d9b05852b66ccc25f96f23d8651bcab20779861a745bdffa273722"
URL = f"https://github.com/caddyserver/caddy/releases/download/v{VERSION}/caddy_{VERSION}_windows_amd64.zip"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--deps", type=Path, required=True)
    args = parser.parse_args()
    args.deps.mkdir(parents=True, exist_ok=True)
    archive = args.deps / "caddy.zip"
    if not archive.exists():
        urllib.request.urlretrieve(URL, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != ARCHIVE_SHA256:
        raise ValueError(f"upstream archive digest mismatch: {digest}")
    with zipfile.ZipFile(archive) as source:
        (args.deps / "caddy.exe").write_bytes(source.read("caddy.exe"))
        for name in ("LICENSE", "README.md"):
            if name in source.namelist():
                (args.deps / name).write_bytes(source.read(name))
    metadata = {"version": VERSION, "url": URL, "archive_sha256": digest,
                "binary_sha256": hashlib.sha256((args.deps / "caddy.exe").read_bytes()).hexdigest(),
                "checksum_source": f"https://github.com/caddyserver/caddy/releases/expanded_assets/v{VERSION}"}
    (args.deps / "upstream.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata))


if __name__ == "__main__":
    main()
