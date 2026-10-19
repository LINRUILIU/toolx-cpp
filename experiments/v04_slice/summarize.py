"""Aggregate actual slice evidence without treating process time as model cost."""
import argparse
import ast
import csv
import hashlib
import json
from pathlib import Path
import statistics
import shutil
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    records = json.loads((root / "summary.json").read_text(encoding="utf-8"))
    if len(records) != 27 or not all(x["oracle"]["passed"] for x in records):
        raise ValueError("the complete 27-case comparison has not passed")
    rows = []
    for result in records:
        rows.append({"arm": result["arm"], "scenario": result["scenario"], "status": result["status"],
                     "oracle_passed": result["oracle"]["passed"], **result["metrics"],
                     "input_sha256": result["input_sha256"],
                     "archive_sha256": (result.get("current_artifact") or {}).get("sha256", "")})
    with (root / "metrics.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    comparison = {}
    for arm in "ABC":
        subset = [x for x in records if x["arm"] == arm]
        successes = [x for x in subset if x["status"] == "succeeded"]
        warm = [x for x in successes if x["scenario"] != "normal"]
        comparison[arm] = {
            "outcome_checks_passed": len(subset), "tasks_succeeded": len(successes),
            "expected_failures": len(subset) - len(successes),
            "normal_s": next(x["metrics"]["elapsed_s"] for x in subset if x["scenario"] == "normal"),
            "warm_success_median_s": statistics.median(x["metrics"]["elapsed_s"] for x in warm),
            "successful_external_commands": sorted({x["metrics"]["child_calls"] for x in successes}),
            "oracle_external_commands_per_success": sorted({x["metrics"]["oracle_child_calls"] for x in successes}),
            "normal_command_output_bytes": next(x["metrics"]["output_bytes"] for x in subset if x["scenario"] == "normal"),
            "normal_runner_extra_hash_checks": next(x["metrics"]["runner_artifact_checks"] for x in subset if x["scenario"] == "normal"),
            "actual_fixture_repairs": sum(x["metrics"]["actual_fixture_repair_actions"] for x in subset),
            "fresh_attempt_retries": sum(x["metrics"]["fresh_attempt_retry"] for x in subset),
        }
    inputs_equal = {}
    for scenario in {x["scenario"] for x in records}:
        inputs_equal[scenario] = len({x["input_sha256"] for x in records if x["scenario"] == scenario}) == 1
    if not all(inputs_equal.values()):
        raise ValueError(f"unequal source inputs: {inputs_equal}")
    comparison["inputs_equal_by_scenario"] = inputs_equal
    code = {}
    for path in (root / "code").glob("*.py"):
        text = path.read_text(encoding="utf-8")
        functions = {node.name: node.end_lineno - node.lineno + 1 for node in ast.parse(text).body
                     if isinstance(node, (ast.FunctionDef, ast.ClassDef))}
        code[path.name] = {"physical_lines": len(text.splitlines()), "bytes": path.stat().st_size,
                          "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "functions": functions}
    comparison["code"] = code
    (root / "comparison.json").write_text(json.dumps(comparison, indent=2), encoding="utf-8")
    bundle = root / "evidence.zip"
    files = [root / name for name in ("environment.json", "predeclared.json", "summary.json", "metrics.csv", "comparison.json")]
    files += list((root / "code").glob("*.py"))
    material = root / "shared-project-material"
    material.mkdir(exist_ok=True)
    for name in ("LICENSE.txt", "readme.md", "CMakeLists.txt", "test/CMakeLists.txt",
                 "cmake/tinyxml2-config.cmake", ".github/workflows/test.yml"):
        target = material / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / "A/source" / name, target)
        files.append(target)
    for arm in "ABC":
        files.append(root / arm / "release/tinyxml2-sdk.tar")
        for run in (root / arm / "runs").iterdir():
            files.extend(run.glob("*.json"))
            files.extend(run.glob("*.jsonl"))
            files.extend(run.glob("*.xml"))
            files.extend((run / "logs").glob("*.txt"))
            files.extend((run / "package").glob("*.json"))
            executable = run / "consumer/Release/xmltest.exe"
            if executable.is_file():
                files.append(executable)
    with zipfile.ZipFile(bundle, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, path.relative_to(root))
    comparison["evidence_zip"] = {"path": str(bundle), "bytes": bundle.stat().st_size,
                                   "sha256": hashlib.sha256(bundle.read_bytes()).hexdigest()}
    print(json.dumps(comparison, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
