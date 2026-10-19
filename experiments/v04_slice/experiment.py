"""Deterministic TinyXML2 release slice, equal project knowledge in all three arms."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import time

from pack_operation import package
from runner import execute

COMMIT = "321ea883b7190d4e85cae5512a12e5eaa8f8731f"
GOOD = b"And Robin shall restore amends."
BAD = b"And Robin shall restore regressions."
GENERATOR = "Visual Studio 18 2026"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, value: dict | list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def inventory(root: Path, names: list[str]) -> dict:
    return {name: sha(root / name) for name in names if (root / name).is_file()}


def prepare_consumer(source: Path, target: Path) -> None:
    """Reuse upstream tests without the adjacent source header masking SDK defects."""
    target.mkdir(parents=True)
    shutil.copyfile(source / "xmltest.cpp", target / "xmltest.cpp")
    shutil.copytree(source / "resources", target / "resources", ignore=shutil.ignore_patterns("out"))
    (target / "resources/out").mkdir()
    text = (source / "test/CMakeLists.txt").read_text(encoding="utf-8")
    text = text.replace("find_package(tinyxml2 REQUIRED)",
                        "find_package(tinyxml2 CONFIG REQUIRED COMPONENTS static)")
    text = text.replace("../xmltest.cpp", "xmltest.cpp")
    text = text.replace('"${CMAKE_CURRENT_SOURCE_DIR}/.."', '"${CMAKE_CURRENT_SOURCE_DIR}"')
    (target / "CMakeLists.txt").write_text(text, encoding="utf-8")


def cache_uses_prefix(build: Path, prefix: Path) -> bool:
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("tinyxml2_DIR:"):
            found = Path(line.split("=", 1)[1]).resolve()
            return found.is_relative_to(prefix.resolve())
    return False


class Calls:
    def __init__(self, directory: Path):
        self.directory = directory
        self.records = []
        directory.mkdir(parents=True)

    def __call__(self, step: str, argv: list[str], cwd: Path | None = None) -> dict:
        number = len(self.records) + 1
        stdout = self.directory / f"{number:02d}-{step}.stdout.txt"
        stderr = self.directory / f"{number:02d}-{step}.stderr.txt"
        start = time.perf_counter()
        try:
            with stdout.open("wb") as out, stderr.open("wb") as err:
                process = subprocess.run(argv, cwd=cwd, stdout=out, stderr=err, timeout=120, check=False)
            code, reason = process.returncode, None
        except subprocess.TimeoutExpired:
            # This slice makes no process-tree containment claim.
            code, reason = -1, "timeout; descendant cleanup not guaranteed by this prototype"
        except OSError as error:
            code, reason = -2, str(error)
        record = {"id": step, "argv": argv, "cwd": str(cwd) if cwd else None,
                  "status": "succeeded" if code == 0 else "failed", "exit_code": code,
                  "duration_s": round(time.perf_counter() - start, 6),
                  "stdout_path": str(stdout), "stderr_path": str(stderr), "error": reason}
        self.records.append(record)
        save(self.directory.parent / "calls.json", self.records)
        return record


def project_steps(source: Path, build: Path, run: Path, final: Path, calls: Calls,
                  arm: str, binary: Path, blocked: bool, state: dict) -> list[dict]:
    """The same recipe and commands are used by the imperative and runner arms."""
    install, extracted, consumer = run / "install", run / "relocated", run / "consumer"
    consumer_source = run / "consumer-source"
    candidate = run / "candidate-sdk.tar"

    def command(identifier: str, argv: list[str], cwd: Path | None = None) -> dict:
        return {"id": identifier, "action": lambda _: calls(identifier, argv, cwd)}

    def packing(_: dict) -> dict:
        state["package"] = package(install, run / "package", candidate, calls,
                                   binary if arm in ("B", "C") else None, blocked)
        result = state["package"]
        artifact = result.get("current_artifact")
        return {"status": result["status"], "package_receipt": str(run / "package/package-result.json"),
                "artifacts": {"archive": artifact} if artifact else {}}

    def relocate(artifacts: dict) -> dict:
        # A/B use an explicit recipe check; C receives a typed artifact reference.
        artifact = artifacts.get("package.archive") or state["package"].get("current_artifact")
        if not artifact or sha(Path(artifact["path"])) != artifact["sha256"]:
            return {"status": "failed", "error": "no current verified archive"}
        extracted.mkdir()
        with tarfile.open(artifact["path"], "r:") as archive:
            archive.extractall(extracted, filter="data")
        prepare_consumer(source, consumer_source)
        if state.get("consumer_failure"):
            header = extracted / "include/tinyxml2.h"
            header.write_bytes(b"#error controlled relocated-header corruption\n" + header.read_bytes())
        return {"status": "succeeded", "path": str(extracted)}

    def publish(artifacts: dict) -> dict:
        artifact = artifacts.get("package.archive") or state["package"].get("current_artifact")
        if not artifact or sha(Path(artifact["path"])) != artifact["sha256"]:
            return {"status": "failed", "error": "candidate changed before final publication"}
        temporary = run / "release-ready.tar"
        shutil.copyfile(artifact["path"], temporary)
        os.replace(temporary, final)
        state["published"] = {"path": str(final), "sha256": sha(final)}
        return {"status": "succeeded", "artifacts": {"archive": state["published"]}}

    steps = [
        command("configure", ["cmake", "-S", str(source), "-B", str(build), "-G", GENERATOR,
                              "-A", "x64", "-DBUILD_SHARED_LIBS=OFF", "-Dtinyxml2_BUILD_TESTING=ON"]),
        command("build", ["cmake", "--build", str(build), "--config", "Release", "--parallel", "2"]),
        command("test", ["ctest", "--test-dir", str(build), "-C", "Release", "--output-on-failure"]),
        command("install", ["cmake", "--install", str(build), "--config", "Release", "--prefix", str(install)]),
        {"id": "package", "action": packing},
        {"id": "relocate", "requires": ["package.archive"], "action": relocate},
        command("consumer_configure", ["cmake", "-S", str(consumer_source), "-B", str(consumer),
                "-G", GENERATOR, "-A", "x64", f"-DCMAKE_PREFIX_PATH={extracted}",
                "-Dtinyxml2_SHARED_LIBS=NO", "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"]),
        command("consumer_build", ["cmake", "--build", str(consumer), "--config", "Release", "--parallel", "2"]),
        command("consumer_test", ["ctest", "--test-dir", str(consumer), "-C", "Release", "--output-on-failure"]),
        {"id": "publish", "requires": ["package.archive"], "action": publish},
    ]
    return steps


def imperative(steps: list[dict]) -> dict:
    """A competent reusable project script: failure gate, current artifact only."""
    records, artifacts = [], {}
    failed = False
    for step in steps:
        if failed:
            records.append({"id": step["id"], "status": "skipped", "reason": "dependency_failed"})
            continue
        try:
            result = step["action"](artifacts)
        except Exception as error:
            result = {"status": "failed", "error": f"{type(error).__name__}: {error}"}
        records.append({"id": step["id"], **result})
        failed = result["status"] != "succeeded"
        for name, artifact in result.get("artifacts", {}).items():
            artifacts[f"{step['id']}.{name}"] = artifact
    return {"status": "failed" if failed else "succeeded", "steps": records, "artifacts": artifacts}


def oracle(result: dict, calls: Calls) -> dict:
    """Independent outcome checks, including real relocated upstream consumption."""
    scenario = result["scenario"]
    expect_success = scenario in ("normal", "repair_test", "repair_archive", "repair_consumer", "warm_repeat")
    checks = {"expected_status": (result["status"] == "succeeded") == expect_success}
    final, old = Path(result["paths"]["final_archive"]), result["previous_artifact"]
    if expect_success:
        artifact = result["current_artifact"]
        checks["current_artifact_bound"] = bool(artifact and sha(final) == artifact["sha256"])
        # Compare tar contents directly to this run's install tree (not ToolX receipt).
        install = Path(result["paths"]["run"]) / "install"
        wanted = {str(p.relative_to(install)).replace("\\", "/"): sha(p)
                  for p in install.rglob("*") if p.is_file()}
        got = {}
        with tarfile.open(final, "r:") as archive:
            for member in archive.getmembers():
                if member.isfile():
                    name = member.name.removeprefix("./")
                    stream = archive.extractfile(member)
                    got[name] = hashlib.sha256(stream.read()).hexdigest()
        checks["archive_matches_current_install"] = wanted == got
        accepted = {x["id"]: x for x in result["commands"]}
        checks["upstream_tests_really_passed"] = accepted["test"]["exit_code"] == 0
        checks["relocated_upstream_consumer_passed"] = accepted["consumer_test"]["exit_code"] == 0
        checks["consumer_used_this_prefix"] = cache_uses_prefix(
            Path(result["paths"]["run"]) / "consumer", Path(result["paths"]["run"]) / "relocated")
        checks["no_adjacent_source_header"] = not (
            Path(result["paths"]["run"]) / "consumer-source/tinyxml2.h").exists()
        # Re-run an independently configured consumer against a second extraction.
        root = Path(result["paths"]["run"]) / "oracle"
        prefix, build = root / "prefix", root / "consumer"
        prefix.mkdir(parents=True)
        with tarfile.open(final, "r:") as archive:
            archive.extractall(prefix, filter="data")
        source = Path(result["paths"]["source"])
        consumer_source = root / "consumer-source"
        prepare_consumer(source, consumer_source)
        stages = [
            ["cmake", "-S", str(consumer_source), "-B", str(build), "-G", GENERATOR, "-A", "x64",
             f"-DCMAKE_PREFIX_PATH={prefix}", "-Dtinyxml2_SHARED_LIBS=NO", "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"],
            ["cmake", "--build", str(build), "--config", "Release", "--parallel", "2"],
            ["ctest", "--test-dir", str(build), "-C", "Release", "--output-on-failure"]]
        observed = [calls(f"oracle_{index}", command) for index, command in enumerate(stages)]
        checks["independent_relocated_consumer_passed"] = all(x["exit_code"] == 0 for x in observed)
        checks["oracle_used_this_prefix"] = cache_uses_prefix(build, prefix)
    else:
        checks["no_current_accepted_artifact"] = result["current_artifact"] is None
        checks["old_archive_unchanged_or_no_new_archive"] = (
            final.is_file() and sha(final) == old["sha256"] if old else not final.exists())
        if scenario == "stale_archive":
            checks["old_archive_was_present"] = old is not None
        if scenario in ("test_failure", "stale_archive"):
            test = next(x for x in result["commands"] if x["id"] == "test")
            output = Path(test["stdout_path"]).read_text(encoding="utf-8", errors="replace")
            checks["real_upstream_test_failed"] = test["exit_code"] != 0 and ", Fail 4" in output
            checks["downstream_not_run"] = all(x["status"] == "skipped" for x in result["steps"][3:])
        if scenario == "partial_archive":
            work = Path(result["paths"]["run"]) / "package"
            checks["partial_stage_exists"] = (work / "stage/include/tinyxml2.h").is_file()
            checks["stage_content_matches_install"] = sha(work / "stage/include/tinyxml2.h") == sha(
                Path(result["paths"]["run"]) / "install/include/tinyxml2.h")
            checks["archive_failed_after_stage"] = result["package"]["stages"]["stage"]["status"] == "succeeded" and result["package"]["stages"]["archive"]["status"] == "failed"
        if scenario == "consumer_failure":
            build = next(x for x in result["commands"] if x["id"] == "consumer_build")
            checks["real_consumer_compilation_failed"] = build["exit_code"] != 0
            checks["verified_candidate_retained"] = (Path(result["paths"]["run"]) / "candidate-sdk.tar").is_file()
            checks["final_publication_skipped"] = result["steps"][-1]["id"] == "publish" and result["steps"][-1]["status"] == "skipped"
    return {"passed": all(checks.values()), "checks": checks}


def run_case(arm: str, scenario: str, root: Path, source: Path, tracked: list[str], binary: Path) -> dict:
    armroot = root / arm
    run = armroot / "runs" / scenario
    run.mkdir(parents=True)  # no overwrite or deletion of prior run evidence
    calls = Calls(run / "logs")
    final = armroot / "release/tinyxml2-sdk.tar"
    final.parent.mkdir(exist_ok=True)
    if scenario == "test_failure":
        final = final.parent / "never-accepted-sdk.tar"
    before = {"path": str(final), "sha256": sha(final)} if final.is_file() else None
    fixture = source / "resources/dream.xml"
    current = fixture.read_bytes()
    wanted = current.replace(BAD, GOOD)
    if scenario in ("test_failure", "stale_archive"):
        wanted = wanted.replace(GOOD, BAD)
    fixture.write_bytes(wanted)
    fixture_changed = current != wanted
    inputs = inventory(source, tracked)
    save(run / "input-manifest.json", inputs)
    shutil.copyfile(fixture, run / "dream.input.xml")
    input_digest = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    state = {"consumer_failure": scenario == "consumer_failure"}
    steps = project_steps(source, armroot / "build", run, final, calls, arm, binary,
                          scenario == "partial_archive", state)
    start = time.perf_counter()
    outcome = execute(steps, run / "ledger.jsonl") if arm == "C" else imperative(steps)
    elapsed = time.perf_counter() - start
    pkg = state.get("package")
    result = {"arm": arm, "scenario": scenario, "upstream_commit": COMMIT, **outcome,
              "input_sha256": input_digest, "paths": {"source": str(source), "run": str(run),
              "final_archive": str(final)}, "previous_artifact": before, "package": pkg,
              "current_artifact": state.get("published") if outcome["status"] == "succeeded" else None,
              "commands": list(calls.records), "metrics": {"elapsed_s": round(elapsed, 6),
              "child_calls": len(calls.records), "top_level_recipe_calls": 1,
              "cache": "cold" if scenario == "normal" else "warm",
              "fixture_edit_actions": int(fixture_changed),
              "actual_fixture_repair_actions": int(scenario == "repair_test" and fixture_changed),
              "fresh_attempt_retry": scenario in ("repair_test", "repair_archive", "repair_consumer"),
              "existing_failed_output_repair_actions": 0,
              "runner_artifact_checks": outcome.get("runner_artifact_checks", 0),
              "package_python_actions": pkg.get("python_actions", 0) if pkg else 0}}
    save(run / "result.json", result)
    oracle_start = time.perf_counter()
    verification = oracle(result, calls)
    result["oracle"] = verification
    result["metrics"]["oracle_elapsed_s"] = round(time.perf_counter() - oracle_start, 6)
    result["metrics"]["oracle_child_calls"] = len(calls.records) - result["metrics"]["child_calls"]
    result["metrics"]["output_bytes"] = sum(Path(x[key]).stat().st_size for x in result["commands"]
        for key in ("stdout_path", "stderr_path") if Path(x[key]).exists())
    save(run / "result.json", result)
    print(json.dumps({"arm": arm, "scenario": scenario, "status": result["status"],
          "oracle": verification["passed"], **result["metrics"]}), flush=True)
    if not verification["passed"]:
        raise RuntimeError(f"outcome oracle failed: {arm} {scenario}: {verification}")
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--toolx-pack", type=Path, required=True)
    args = parser.parse_args()
    upstream, root, binary = args.upstream.resolve(), args.output.resolve(), args.toolx_pack.resolve()
    root.mkdir(parents=True)  # deliberate exclusive experiment root
    code = root / "code"
    code.mkdir()
    for path in Path(__file__).parent.glob("*.py"):
        shutil.copyfile(path, code / path.name)
    tracked = subprocess.check_output(["git", "-C", str(upstream), "ls-files"], text=True).splitlines()
    observed_commit = subprocess.check_output(["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip()
    if observed_commit != COMMIT:
        raise ValueError("unexpected upstream revision")
    save(root / "environment.json", {"upstream_commit": observed_commit, "toolx_pack": str(binary),
         "toolx_sha256": sha(binary), "python": sys.version, "platform": sys.platform,
         "generator": GENERATOR, "cmake": subprocess.check_output(["cmake", "--version"], text=True),
         "experiment_code": {path.name: sha(path) for path in code.glob("*.py")},
         "toolx_source_revision": "24466b2 (existing local build; binary identity is recorded independently)",
         "scope": "deterministic engineering slice; no model/token adoption experiment"})
    save(root / "predeclared.json", {"arms": {"A": "mature recipe + shutil/cmake tar",
         "B": "same recipe + verified ToolX packaging operation", "C": "same recipe/operation + minimal runner"},
         "cases": ["normal", "test_failure", "stale_archive", "repair_test", "partial_archive", "repair_archive", "consumer_failure", "repair_consumer", "warm_repeat"],
         "decision": "runner requires outcome/repair benefit beyond B; shared recipe benefits are not runner benefits",
         "oracle": "upstream tests, archive inventory/content, relocated upstream consumer, old archive retention, partial stage"})
    sources = {}
    for arm in ("A", "B", "C"):
        source = root / arm / "source"
        shutil.copytree(upstream, source, ignore=shutil.ignore_patterns(".git"))
        sources[arm] = source
    results = []
    cases = ["normal", "test_failure", "stale_archive", "repair_test", "partial_archive", "repair_archive", "consumer_failure", "repair_consumer", "warm_repeat"]
    for index, scenario in enumerate(cases):
        # Rotate order to reduce systematic warm machine ordering bias.
        arms = ("A", "B", "C")
        for arm in arms[index % 3:] + arms[:index % 3]:
            results.append(run_case(arm, scenario, root, sources[arm], tracked, binary))
            save(root / "summary.json", results)


if __name__ == "__main__":
    main()
