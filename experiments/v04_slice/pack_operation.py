"""One verified-package operation shared by all three experiment routes.

This is a local experiment, not a public ToolX protocol. The operation attests
only that the published tar matches the input tree it observed. The project
recipe remains responsible for build/test/install provenance. Input, work and
output trees must remain under exclusive control during this operation.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
from typing import Callable


def package(
    source: Path,
    work: Path,
    final_tar: Path,
    call: Callable,
    toolx_pack: Path | None = None,
    fail_archive: bool = False,
) -> dict:
    """Stage, archive, independently verify, then publish an input tree.

    ``work`` is a caller-owned, fresh attempt directory. A previous final tar
    is retained on failure and is never reported as this attempt's artifact.
    ``call`` records actual child-process results without raising on nonzero
    exits. The native route uses copytree and CMake's mature tar backend; the
    ToolX route uses the existing CLI, with identical verification afterwards.
    """
    source, work, final_tar = map(lambda p: Path(p).absolute(), (source, work, final_tar))
    stage = work / "stage"
    candidate = work / "candidate.tar"
    receipt_path = work / "package-result.json"
    action_log: list[dict] = []
    result = {
        "schema": "toolx.experiment.verified-package.v1",
        "status": "failed",
        "backend": "toolx-pack" if toolx_pack is not None else "copytree+cmake-tar",
        "source": {"path": str(source)},
        "stage_path": str(stage),
        "candidate_path": str(candidate),
        "receipt_path": str(receipt_path),
        "stages": {
            name: {"status": "not_started"}
            for name in ("stage", "archive", "verification", "commit")
        },
        "current_artifact": None,
        "previous_artifact": None,
        "committed": False,
        "python_actions": 0,
        "python_action_log": action_log,
        "error": None,
    }
    active_phase = "stage"

    def action(name: str, **details) -> None:
        # Count semantic Python actions as well as recorded external commands.
        # This is not a syscall count or a claim about execution complexity.
        action_log.append({"action": name, **details})
        result["python_actions"] = len(action_log)

    def sha256_file(path: Path) -> str:
        action("hash_file", path=str(path))
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def is_link(path: Path) -> bool:
        info = path.lstat()
        return stat.S_ISLNK(info.st_mode) or bool(
            getattr(info, "st_file_attributes", 0)
            & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
        )

    def inventory(root: Path) -> tuple[list[dict], str]:
        action("inventory_tree", path=str(root))
        if is_link(root) or not root.is_dir():
            raise ValueError(f"input tree must be a regular directory: {root}")
        entries = []
        for directory, directories, files in os.walk(root, followlinks=False):
            directories.sort()
            files.sort()
            for name in directories + files:
                path = Path(directory) / name
                if is_link(path):
                    raise ValueError(f"links/reparse points are unsupported: {path}")
                relative = path.relative_to(root).as_posix()
                if path.is_dir():
                    entries.append({"path": relative, "type": "directory"})
                elif path.is_file():
                    entries.append(
                        {
                            "path": relative,
                            "type": "file",
                            "size": path.stat().st_size,
                            "sha256": sha256_file(path),
                        }
                    )
                else:
                    raise ValueError(f"special input files are unsupported: {path}")
        entries.sort(key=lambda item: item["path"])
        encoded = json.dumps(entries, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        return entries, hashlib.sha256(encoded).hexdigest()

    def contained(child: Path, parent: Path) -> bool:
        try:
            child.resolve().relative_to(parent.resolve())
            return True
        except ValueError:
            return False

    def run(name: str, argv: list[str], cwd: Path | None = None) -> dict:
        record = call("package_" + name, argv, cwd=cwd)
        result["stages"][name]["call"] = record
        if record.get("status") != "succeeded" or record.get("exit_code") != 0:
            raise RuntimeError(f"{name} process failed with exit code {record.get('exit_code')}")
        return record

    def verify_tar(expected: list[dict]) -> None:
        action("verify_tar_members_and_hashes", path=str(candidate))
        observed = []
        seen = set()
        with tarfile.open(candidate, mode="r:") as archive:
            for member in archive:
                name = member.name
                while name.startswith("./"):
                    name = name[2:]
                if name in ("", ".") and member.isdir():
                    continue  # CMake's tar includes an explicit root directory.
                name = name.rstrip("/") if member.isdir() else name
                parts = PurePosixPath(name).parts
                if (
                    not name
                    or name.startswith("/")
                    or "\\" in name
                    or any(part in ("", ".", "..") for part in name.split("/"))
                    or ":" in parts[0]
                    or name in seen
                ):
                    raise ValueError(f"unsafe or duplicate tar member: {member.name!r}")
                seen.add(name)
                if member.isdir():
                    observed.append({"path": name, "type": "directory"})
                elif member.isfile() and not member.issym() and not member.islnk():
                    stream = archive.extractfile(member)
                    if stream is None:
                        raise ValueError(f"tar member has no readable payload: {name}")
                    digest = hashlib.sha256()
                    size = 0
                    with stream:
                        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                            size += len(chunk)
                            digest.update(chunk)
                    if size != member.size:
                        raise ValueError(f"tar payload length differs from header: {name}")
                    observed.append({"path": name, "type": "file", "size": size, "sha256": digest.hexdigest()})
                else:
                    raise ValueError(f"links and special tar members are unsupported: {name}")
        observed.sort(key=lambda item: item["path"])
        if observed != expected:
            expected_by_name = {entry["path"]: entry for entry in expected}
            actual_by_name = {entry["path"]: entry for entry in observed}
            missing = sorted(expected_by_name.keys() - actual_by_name.keys())
            extra = sorted(actual_by_name.keys() - expected_by_name.keys())
            changed = sorted(
                name for name in expected_by_name.keys() & actual_by_name.keys()
                if expected_by_name[name] != actual_by_name[name]
            )
            raise ValueError(f"archive differs from input: missing={missing}, extra={extra}, changed={changed}")

    def finish() -> dict:
        for details in result["stages"].values():
            if details["status"] == "not_started":
                details["status"] = "skipped"
        action("write_receipt", path=str(receipt_path))
        temporary_receipt = work / "package-result.json.tmp"
        temporary_receipt.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary_receipt, receipt_path)
        return result

    try:
        action("prepare_work_directory", path=str(work))
        work.mkdir(parents=True, exist_ok=True)
        if stage.exists() or stage.is_symlink() or candidate.exists() or candidate.is_symlink():
            raise ValueError("work must have fresh stage and candidate paths; existing artifacts are not reused")
        if contained(work, source) or contained(source, work):
            raise ValueError("source and work trees must be disjoint")
        if contained(final_tar, source) or contained(final_tar, work):
            raise ValueError("final tar must be outside source and work trees")
        if final_tar.exists() or final_tar.is_symlink():
            if is_link(final_tar) or not final_tar.is_file():
                raise ValueError("previous final artifact must be a regular file")
            result["previous_artifact"] = {"path": str(final_tar), "sha256": sha256_file(final_tar)}
        entries, tree_sha256 = inventory(source)
        result["source"].update({"tree_sha256": tree_sha256, "inventory": entries})
        result["stages"]["stage"]["status"] = "running"
        if toolx_pack is None:
            action("copytree", source=str(source), destination=str(stage))
            shutil.copytree(source, stage)
        else:
            run("stage", [str(toolx_pack), "stage", "--json", "--src", str(source), "--out", str(stage)])
            # The old CLI selects regular files and omits empty directories.
            # Preserve them explicitly rather than weaken this operation's
            # equality check. This Python work is included in the cost ledger.
            for entry in entries:
                if entry["type"] == "directory":
                    directory = stage / entry["path"]
                    if not directory.exists():
                        action("preserve_empty_directory", path=str(directory))
                        directory.mkdir(parents=True, exist_ok=True)
        staged_entries, staged_digest = inventory(stage)
        if staged_entries != entries:
            raise ValueError("stage differs from the observed input tree")
        result["stages"]["stage"].update({"status": "succeeded", "tree_sha256": staged_digest})

        active_phase = "archive"
        result["stages"]["archive"]["status"] = "running"
        if fail_archive:
            action("create_archive_io_obstacle", path=str(candidate))
            candidate.mkdir()
        if toolx_pack is None:
            run("archive", ["cmake", "-E", "tar", "cf", str(candidate), "--format=gnutar", "."], cwd=stage)
        else:
            run("archive", [str(toolx_pack), "archive", "--json", "--src", str(stage), "--archive", str(candidate)])
        result["stages"]["archive"]["status"] = "succeeded"

        active_phase = "verification"
        result["stages"]["verification"]["status"] = "running"
        verify_tar(entries)
        latest_entries, latest_digest = inventory(source)
        if latest_entries != entries:
            raise ValueError("source changed during packaging; this attempt cannot attest a stable input")
        result["stages"]["verification"].update({"status": "succeeded", "source_tree_sha256": latest_digest})
        archive_sha256 = sha256_file(candidate)

        active_phase = "commit"
        result["stages"]["commit"]["status"] = "running"
        action("prepare_publish_directory", path=str(final_tar.parent))
        final_tar.parent.mkdir(parents=True, exist_ok=True)
        action("atomic_publish_tar", source=str(candidate), destination=str(final_tar))
        os.replace(candidate, final_tar)
        result["current_artifact"] = {"path": str(final_tar), "sha256": archive_sha256, "source_tree_sha256": tree_sha256}
        result["committed"] = True
        result["stages"]["commit"]["status"] = "succeeded"
        result["status"] = "succeeded"
    except (OSError, ValueError, RuntimeError, tarfile.TarError) as exc:
        result["stages"][active_phase]["status"] = "failed"
        result["error"] = {"phase": active_phase, "type": type(exc).__name__, "message": str(exc)}
    return finish()
