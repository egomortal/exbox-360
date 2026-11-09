"""T13 private asset/provenance tests. No source files are modified."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_small_items import png_entry
from build_era_library import RESOURCES, unpack


class SmallItems(unittest.TestCase):
    def test_xdbf_entries_and_staged_images(self):
        evidence = json.loads((ROOT / "private/t13-source/extraction.json").read_text())
        self.assertEqual(len(evidence["images"]), 6)
        for row in evidence["images"]:
            ident, png = png_entry(Path(row["source"]).read_bytes())
            self.assertEqual(ident, row["entry_id"])
            self.assertEqual(png, (ROOT / row["file"]).read_bytes())
            self.assertEqual(hashlib.sha256(png).hexdigest(), row["sha256"])
            self.assertEqual(struct.unpack_from(">II", png, 16), (64, 64))

    def test_xdbf_rejects_invalid_entries(self):
        raw = (RESOURCES / "NXE/Retail/9199/dash.xex/FFFE07D1").read_bytes()
        with self.assertRaises(ValueError):
            png_entry(b"wrong")
        damaged = bytearray(raw)
        at = raw.index(b"\x89PNG\r\n\x1a\n")
        damaged[at + 16] ^= 1
        with self.assertRaises(ValueError):
            png_entry(damaged)
        with self.assertRaises(ValueError):
            png_entry(raw[:at + 100])

    def test_provenance_rehashes_owner_members(self):
        evidence = json.loads((ROOT / "private/t13-source/extraction.json").read_text())
        rows = {r["file"]: r for r in json.loads((ROOT / "private/era-skins/sources.json").read_text())}
        cache = {}
        self.assertEqual(sum(r["exact"] for r in evidence["provenance"]), 179)
        self.assertEqual(len(evidence["provenance"]), 189)
        for proof in evidence["provenance"]:
            row = rows[proof["file"]]
            staged = (ROOT / "private/era-skins" / proof["file"]).read_bytes()
            self.assertEqual(hashlib.sha256(staged).hexdigest(), proof["sha256"])
            if not proof["exact"]:
                self.assertFalse(row["exact_build"])
                self.assertEqual(row["source"], proof["previous_source"])
                continue
            self.assertTrue(row["exact_build"])
            self.assertEqual(row["source_build"], row["build"])
            relative = row["source"].removeprefix("decrypted/resources/")
            package, member = relative.rsplit("/", 1)
            if package not in cache:
                cache[package] = unpack((RESOURCES / package).read_bytes())
            self.assertEqual(cache[package][member], staged)

    def test_existing_source_line_endings(self):
        for original in (ROOT / "backups/T13-small-items").rglob("*"):
            if not original.is_file():
                continue
            current = ROOT / original.relative_to(ROOT / "backups/T13-small-items")
            before, after = original.read_bytes(), current.read_bytes()
            self.assertEqual(before.count(b"\n") == before.count(b"\r\n"),
                             after.count(b"\n") == after.count(b"\r\n"), str(current))


if __name__ == "__main__":
    unittest.main()
