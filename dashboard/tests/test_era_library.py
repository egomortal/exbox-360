"""T02 extractor and staging checks; python -B tests/test_era_library.py."""
import ast
import csv
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_era_library as era


def package(entries, version=1):
    header = bytearray(22)
    header[:4] = b"XUIZ"
    struct.pack_into(">I", header, 4, version)
    struct.pack_into(">H", header, 20, len(entries))
    table, data = bytearray(), bytearray(b"padding")
    for name, content in entries:
        encoded = name.encode("utf-16be" if version == 1 else "latin1")
        table += struct.pack(">IIB", len(content), len(data), len(name)) + encoded
        data += content + b"gap"
    return bytes(header + table + data)


class PackageTests(unittest.TestCase):
    def test_offsets_follow_entire_name_table(self):
        entries = [("first.png", b"first bytes"), ("longer_name.png", b"second bytes")]
        self.assertEqual(era.unpack(package(entries)), dict(entries))

    def test_latin1_names_and_slashes(self):
        self.assertEqual(era.unpack(package([("dir\\caf\xe9.png", b"image")], 2)),
                         {"dir/caf\xe9.png": b"image"})

    def test_duplicate_names_match_xuiz_last_entry(self):
        self.assertEqual(era.unpack(package([("a.png", b"old"), ("a.png", b"new")])),
                         {"a.png": b"new"})

    def test_parent_scene_names_stay_in_memory(self):
        self.assertEqual(era.unpack(package([("../handles/a.xur", b"scene")])),
                         {"../handles/a.xur": b"scene"})

    def test_truncated_packages_fail(self):
        raw = package([("a.png", b"image")])
        for broken in (b"bad", raw[:23], raw[:32], raw[:-5]):
            with self.subTest(size=len(broken)), self.assertRaises(ValueError):
                era.unpack(broken)

    def test_unsafe_destinations_fail_before_writing(self):
        for name in ("../outside.png", "/absolute.png", "6770/a/../../b.png", "C:/a.png", "6770/a\\b.png"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                era.write_output(era.ROOT / "private/era-library", {name: b"unused"})

    def test_runtime_name_inventory(self):
        source = (era.ROOT / "main.cpp").read_text(encoding="utf-8")
        paths = era.used_paths(source)
        self.assertEqual(len(paths), 66)  # Existing achievement image + 34 original + 13 network + 7 XMF + 11 picture icons
        self.assertIn("arcade/AchievementImg.png", paths)
        self.assertIn("dashcomm/btn_32x_Volume.png", paths)
        self.assertIn("music/ico_32x_Artist.png", paths)
        self.assertIn("dashcomm/ico_96x_Camera.png", paths)
        self.assertIn("shared/ico_32x_Mail.png", paths)
        self.assertIn("memory/ico_64x_HD.png", paths)
        self.assertIn("memory/ico_64x_AvatarAssets.png", paths)
        self.assertIn("dashcomm/ico_64x_AllDevices.png", paths)

    def test_picture_aliases_and_local_precedence(self):
        self.assertEqual(era.library_source({"pictures": {"ico_32x_RotateRight.png": b"blades"}},
                                           "pictures", "ico_32x_Rotate.png"),
                         (b"blades", "pictures/ico_32x_RotateRight.png"))
        packages = {"pictures": {"ico_32x_Rotate.png": b"kinect"},
                    "dashcomm": {"ico_32x_Rotate.png": b"metro", "btn_32x_Play.png": b"button"},
                    "dvd": {"ico_32x_play_black.png": b"black"}}
        self.assertEqual(era.library_source(packages, "pictures", "ico_32x_Rotate.png"),
                         (b"kinect", "pictures/ico_32x_Rotate.png"))
        self.assertEqual(era.library_source(packages, "pictures", "ico_32x_Play_black.png"),
                         (b"black", "dvd/ico_32x_play_black.png"))
        self.assertEqual(era.library_source({}, "pictures", "ico_32x_Shuffle.png"), (None, ""))
        paths = era.used_paths((era.ROOT / "main.cpp").read_text(encoding="utf-8"))
        self.assertEqual(len([p for p in paths if p.startswith("pictures/")]), 11)

    def test_xmf_common_icons_and_dvd_precedence(self):
        names = ("ico_32x_last.png", "ico_32x_next.png", "ico_32x_stop.png",
                 "ico_32x_FF.png", "ico_32x_RR.png")
        for name in names:
            with self.subTest(name=name):
                self.assertEqual(era.library_source({"dashcomm": {name: b"metro"}}, "dvd", name),
                                 (b"metro", "dashcomm/" + name))
                self.assertEqual(era.library_source({"dvd": {name: b"kinect"},
                                                    "dashcomm": {name: b"common"}}, "dvd", name),
                                 (b"kinect", "dvd/" + name))
        paths = era.used_paths((era.ROOT / "main.cpp").read_text(encoding="utf-8"))
        self.assertEqual(len([p for p in paths if p.startswith("dvd/")]), 7)
        self.assertIn("dvd/ico_32x_play_black.png", paths)
        self.assertIn("dvd/ico_32x_pause_black.png", paths)


class PrivateOutputTests(unittest.TestCase):
    def setUp(self):
        self.output = era.ROOT / "private/era-library"
        manifest = self.output / "manifest.tsv"
        if not manifest.is_file():
            self.skipTest("Run tools/build_era_library.py with the owner's resources first")
        with manifest.open(encoding="utf-8", newline="") as handle:
            self.rows = list(csv.DictReader(handle, delimiter="\t"))

    def test_manifest_hashes_and_only_differences(self):
        listed = set()
        for row in self.rows:
            relative = row["build"] + "/" + row["path"]
            self.assertNotIn(relative, listed)
            listed.add(relative)
            data = (self.output / relative).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), row["sha256"])
            # Network aliases are staged even when equal; collect() already requires this.
            if not row["path"].startswith("network/"):
                self.assertNotEqual(data, (era.ROOT / "private/xbox/unpacked" / row["path"]).read_bytes())
        actual = {p.relative_to(self.output).as_posix() for p in self.output.rglob("*")
                  if p.is_file() and p.name != "manifest.tsv"}
        self.assertEqual(listed, actual)

    def test_all_available_source_differences_are_extracted(self):
        if not era.RESOURCES.is_dir():
            self.skipTest("Owner's decrypted packages are unavailable")
        paths = era.used_paths((era.ROOT / "main.cpp").read_text(encoding="utf-8"))
        files, _ = era.collect(era.RESOURCES, era.ROOT / "private/xbox/unpacked", paths)
        self.assertEqual(set(files), {r["build"] + "/" + r["path"] for r in self.rows})
        for relative, data in files.items():
            self.assertEqual(data, (self.output / relative).read_bytes())

    def test_deploy_destinations_without_ftp(self):
        tree = ast.parse((era.ROOT / "deploy.py").read_text(encoding="utf-8"))
        nodes = []
        for index, node in enumerate(tree.body):
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "era_library" for t in node.targets):
                nodes = tree.body[index:index + 2]
                break
        self.assertEqual(len(nodes), 2)
        namespace = {"root": era.ROOT, "files": {}}
        exec(compile(ast.Module(body=nodes, type_ignores=[]), "deploy.py era staging", "exec"), namespace)
        files = namespace["files"]
        expected = {f'private_library/eras/{r["build"]}/{r["path"]}': self.output / r["build"] / r["path"]
                    for r in self.rows}
        self.assertEqual(files, expected)
        self.assertFalse(any("manifest.tsv" in name for name in files))


if __name__ == "__main__":
    unittest.main()
