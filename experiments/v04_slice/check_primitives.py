"""Focused regressions for artifact binding, not an end-to-end substitute."""
from pathlib import Path
from contextlib import contextmanager
import tempfile
import unittest

from runner import digest, execute


@contextmanager
def controlled_directory():
    workspace = Path(__file__).resolve().parents[2] / "temp/v04-slice-primitives"
    workspace.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.TemporaryDirectory(dir=workspace)
    try:
        yield temporary.name
    finally:
        if not Path(temporary.name).resolve().is_relative_to(workspace.resolve()):
            raise ValueError("refusing cleanup outside the experiment workspace")
        temporary.cleanup()


class ArtifactBindingTests(unittest.TestCase):
    def test_wrong_producer_identity_is_rejected(self):
        with controlled_directory() as directory:
            root = Path(directory)
            artifact = root / "artifact"
            artifact.write_bytes(b"actual")
            steps = [{"id": "produce", "action": lambda _: {
                "status": "succeeded", "artifacts": {"file": {"path": str(artifact), "sha256": "wrong"}}}},
                {"id": "consume", "action": lambda _: {"status": "succeeded"}}]
            result = execute(steps, root / "ledger")
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["steps"][1]["status"], "skipped")
            self.assertEqual(result["artifacts"], {})

    def test_missing_producer_artifact_becomes_recorded_failure(self):
        with controlled_directory() as directory:
            root = Path(directory)
            result = execute([{"id": "produce", "action": lambda _: {
                "status": "succeeded", "artifacts": {"file": {
                    "path": str(root / "missing"), "sha256": "missing"}}}}], root / "ledger")
            self.assertEqual(result["status"], "failed")
            self.assertIn("FileNotFoundError", result["steps"][0]["error"])

    def test_changed_dependency_is_rejected_before_consumer(self):
        with controlled_directory() as directory:
            root = Path(directory)
            artifact = root / "artifact"
            artifact.write_bytes(b"initial")
            expected = digest(artifact)

            def mutate(_):
                artifact.write_bytes(b"changed")
                return {"status": "succeeded"}

            def forbidden(_):
                self.fail("consumer ran with a changed dependency")

            steps = [
                {"id": "produce", "action": lambda _: {"status": "succeeded", "artifacts": {
                    "file": {"path": str(artifact), "sha256": expected}}}},
                {"id": "mutate", "action": mutate},
                {"id": "consume", "requires": ["produce.file"], "action": forbidden}]
            result = execute(steps, root / "ledger")
            self.assertEqual(result["steps"][-1]["status"], "failed")
            self.assertIn("artifact changed", result["steps"][-1]["error"])


if __name__ == "__main__":
    unittest.main()
