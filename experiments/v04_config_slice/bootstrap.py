"""Fetch a pinned upstream binary and verify the published release checksum."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

VERSION = "1.12.12"
ARCHIVE_SHA256 = "352c69526b76cd199539c0e02b4042d1acc109cde3a0a671a40af4e24290faa4"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def fetch(url):
        with opener.open(urllib.request.Request(url, headers={"User-Agent": "ToolX-config-validation"}), timeout=90) as response:
            return response.read()

    name = f"sing-box-{VERSION}-windows-amd64.zip"
    asset_url = f"https://github.com/SagerNet/sing-box/releases/download/v{VERSION}/{name}"
    # Pinned digest published on GitHub's expanded release-assets page. Avoid
    # depending on the anonymous API quota for this repeatable experiment.
    expected = ARCHIVE_SHA256
    archive_path = root / name
    data = archive_path.read_bytes() if archive_path.exists() else fetch(asset_url)
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError("upstream archive checksum mismatch")
    archive_path.write_bytes(data)
    with zipfile.ZipFile(archive_path) as archive:
        binary = next(entry for entry in archive.namelist() if Path(entry).name == "sing-box.exe")
        (root / "sing-box.exe").write_bytes(archive.read(binary))
        for entry in archive.namelist():
            if Path(entry).name.lower().startswith("license"):
                (root / "upstream-LICENSE.txt").write_bytes(archive.read(entry))
    for filename in ("cmd_run.go", "cmd_merge.go", "cmd.go"):
        try:
            content = fetch(f"https://raw.githubusercontent.com/SagerNet/sing-box/v{VERSION}/cmd/sing-box/{filename}")
            (root / filename).write_bytes(content)
        except urllib.error.HTTPError:
            pass
    record = {"version": VERSION, "release_url": f"https://github.com/SagerNet/sing-box/releases/tag/v{VERSION}", "archive": name,
              "archive_sha256": actual, "published_checksum_sha256": expected,
              "checksum_source": f"https://github.com/SagerNet/sing-box/releases/expanded_assets/v{VERSION}",
              "binary_sha256": hashlib.sha256((root / "sing-box.exe").read_bytes()).hexdigest(),
              "asset_url": asset_url}
    (root / "upstream.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
