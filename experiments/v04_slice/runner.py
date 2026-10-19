"""Experimental sequential runner. No public protocol or automatic resume."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Callable


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(steps: list[dict], ledger: Path) -> dict:
    """Record intents and observations; check typed file references before use."""
    results, artifacts = [], {}
    stopped = False
    checks = 0

    def record(value: dict) -> None:
        with ledger.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(value, ensure_ascii=False) + "\n")
            stream.flush()

    for step in steps:
        if stopped:
            result = {"id": step["id"], "status": "skipped", "reason": "dependency_failed"}
            results.append(result)
            record(result)
            continue
        record({"id": step["id"], "status": "running", "requires": step.get("requires", [])})
        try:
            for key in step.get("requires", []):
                artifact = artifacts[key]
                path = Path(artifact["path"])
                if not path.is_file() or digest(path) != artifact["sha256"]:
                    raise ValueError(f"artifact changed or missing: {key}")
                checks += 1
            outcome = step["action"](artifacts)
            if outcome["status"] == "succeeded":
                for name, artifact in outcome.get("artifacts", {}).items():
                    path = Path(artifact["path"])
                    actual = digest(path)
                    checks += 1
                    if actual != artifact["sha256"]:
                        raise ValueError(f"producer artifact changed: {step['id']}.{name}")
                    artifacts[f"{step['id']}.{name}"] = {
                        "path": str(path), "sha256": actual, "producer": step["id"]
                    }
        except Exception as error:
            outcome = {"status": "failed", "error": f"{type(error).__name__}: {error}"}
        result = {"id": step["id"], **outcome}
        results.append(result)
        if outcome["status"] != "succeeded":
            stopped = True
        record(result)
    return {"status": "failed" if stopped else "succeeded", "steps": results,
            "artifacts": artifacts, "runner_artifact_checks": checks}
