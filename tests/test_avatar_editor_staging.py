"""Local synthetic checks: never launch the editor or contact the console."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"tools/avatar_editor/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


staging = load("stage")
deploy = load("deployment")


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / "build" / f"av2-python-fixture-{time.time_ns()}"
        self.source = self.root / "source"
        self.dest = self.root / "dest"
        (self.source / "assets/closet").mkdir(parents=True)
        (self.source / "assets/closet/closet_index.tsv").write_text("fixture\n", encoding="utf-8")
        (self.source / "assets/item.bin").write_bytes(b"compressed fixture")
        (self.source / "userdata").mkdir()
        (self.source / "userdata/avatar_manifest.bin").write_bytes(b"save must not ship")
        (self.source / "avatareditor.exe").write_bytes(b"must not ship")

    def stage(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return staging.stage(self.source, self.dest)

    def test_copy_resume_and_deploy_scope(self):
        summary = self.stage()
        before = staging.sha256(self.dest / "staging-manifest.jsonl")
        self.stage()
        self.assertEqual(before, staging.sha256(self.dest / "staging-manifest.jsonl"))
        self.assertEqual(summary["asset_files"], 2)
        self.assertFalse((self.dest / "userdata").exists())
        self.assertFalse((self.dest / "avatareditor.exe").exists())
        files = deploy.avatar_editor_files(self.dest)
        self.assertEqual(len(files), 4)
        self.assertTrue(all(p.startswith("avatar-editor/") for p in files))
        self.assertFalse(any("userdata" in p for p in files))
        (self.source / "assets/item.bin").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            self.stage()
        self.assertEqual((self.dest / "assets/item.bin").read_bytes(), b"compressed fixture")

    def test_corrupt_asset_rejected(self):
        self.stage()
        (self.dest / "assets/item.bin").write_bytes(b"X" * len(b"compressed fixture"))
        with self.assertRaises(ValueError):
            deploy.avatar_editor_files(self.dest)

    def test_unsafe_manifest_rejected(self):
        self.stage()
        manifest = self.dest / "staging-manifest.jsonl"
        with manifest.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"path": "assets/../../userdata/avatar_manifest.bin"}) + "\n")
        summary_path = self.dest / "staging-summary.json"
        summary = json.loads(summary_path.read_text())
        summary["manifest_sha256"] = hashlib.sha256(manifest.read_bytes()).hexdigest()
        summary_path.write_text(json.dumps(summary))
        with self.assertRaises(ValueError):
            deploy.avatar_editor_files(self.dest)


if __name__ == "__main__":
    unittest.main()
