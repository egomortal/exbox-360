"""Contract tests for spec-driven binding generation; no guest assets required."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("editor_hooks", ROOT / "tools/generate_editor_hooks.py")
hooks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hooks)


class HookGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.text = hooks.SPEC.read_text(encoding="utf-8")
        self.line = next(line for line in self.text.splitlines() if line.startswith("| import |"))

    def test_checked_in_outputs_are_current(self):
        rows = hooks.parse(self.text)
        for name, text in hooks.generate(rows).items():
            self.assertEqual((hooks.OUTPUT / name).read_bytes(), text.encode("utf-8"), name)
        self.assertEqual(len(rows), len({row["symbol"] for row in rows}))

    def test_malformed_contracts_fail_before_generation(self):
        cases = {
            "duplicate": self.text.replace(self.line, self.line + "\n" + self.line, 1),
            "unknown_kind": self.text.replace(self.line, self.line.replace("| import |", "| bogus |"), 1),
            "bad_signature": self.text.replace("PPCRegister& r3 | trace", "int* r3 | trace", 1),
            "stub_mismatch": self.text.replace("| - | - | stub |", "| - | - | existing |", 1),
            "missing_marker": self.text.replace("<!-- END NON-GPU BINDING TABLE -->", "", 1),
        }
        for name, text in cases.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                hooks.parse(text)


if __name__ == "__main__":
    unittest.main()
