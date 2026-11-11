"""Checks for the tool provisioner's own logic. No network, nothing downloaded or built.

    python3 -B toolchain/test_provision.py
"""
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import provision as pv

OFFICIAL_HOSTS = ("https://github.com/", "https://apt.llvm.org", "https://zlib.net/")


class Pins(unittest.TestCase):
    def test_every_pin_is_exact(self):
        for item in pv.PINS["sources"] + pv.PINS["git_tools"] + pv.PINS["tool_sources"]:
            self.assertRegex(item["commit"], r"^[0-9a-f]{40}$", item)
        for item in pv.PINS["downloads"] + pv.PINS["python_wheels"]:
            self.assertRegex(item["sha256"], r"^[0-9a-f]{64}$", item)
        self.assertRegex(pv.PINS["native_sdk"]["base_release_sha256"], r"^[0-9a-f]{64}$")
        self.assertIn("2078da43e25a", pv.PINS["platform"]["llvm"]["version"])

    def test_every_source_is_an_official_upstream(self):
        urls = [i.get("git") or i.get("url") for i in
                pv.PINS["sources"] + pv.PINS["git_tools"] + pv.PINS["downloads"] + pv.PINS["tool_sources"]]
        urls += [pv.PINS["native_sdk"]["base_release"], pv.PINS["platform"]["llvm"]["repo"]]
        for url in urls:
            self.assertTrue(url.startswith(OFFICIAL_HOSTS), url)
            self.assertNotIn("egomortal", url)  # nothing rehosted by Ex-Box

    def test_rexglue_and_xextool_come_from_their_official_projects(self):
        tools = {i["role"]: i for i in pv.PINS["tool_sources"]}
        self.assertEqual(tools["rexglue_sdk"]["git"], "https://github.com/rexglue/rexglue-sdk.git")
        self.assertTrue(tools["rexglue_sdk"]["submodules"])
        self.assertEqual(tools["xex-tool"]["git"], "https://github.com/mrtitanic777/XexTool-RE.git")
        fixes = [t for t in pv.PINS["source_transforms"] if t["role"] == "xex-tool"]
        self.assertEqual({t["path"] for t in fixes}, {"src/compress/lzx.cpp"})
        self.assertEqual(fixes[-1].get("line_endings"), "crlf")  # the builder pins the CRLF file

    def test_the_builder_pins_cover_each_role(self):
        counts = {role: len({r["path"] for r in pv.pinned_files(role)}) for role in
                  ("mesa-source", "payload-sdk-source", "vulkan-port-source", "glm-source", "fast-float-source",
                   "native-sdk", "avatar_editor", "rexglue_sdk", "xex-tool")}
        self.assertEqual(counts["native-sdk"], 941)  # 943 entries, 941 distinct files
        self.assertEqual(counts["glm-source"], 179)
        for role, count in counts.items():
            self.assertGreater(count, 0, role)


class Verify(unittest.TestCase):
    def test_missing_and_different_files_are_reported(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "a.h").write_bytes(b"right")
            (root / "b.h").write_bytes(b"wrong")
            import hashlib
            rows = [dict(path="a.h", input_sha256=[hashlib.sha256(b"right").hexdigest()]),
                    dict(path="b.h", input_sha256=[hashlib.sha256(b"right").hexdigest()]),
                    dict(path="c.h", input_sha256=["0" * 64])]
            with mock.patch.object(pv, "pinned_files", return_value=rows):
                count, bad = pv.verify_tree("x", root)
            self.assertEqual(count, 3)
            self.assertEqual(bad, ["different b.h", "missing c.h"])


@unittest.skipUnless(Path("/etc/os-release").is_file(), "Linux only (run inside WSL)")
class Preflight(unittest.TestCase):
    def test_missing_packages_give_exact_commands(self):
        with tempfile.TemporaryDirectory() as d:
            p = pv.Provisioner(Path(d) / "deps", 2, None, None, quiet=True)
            def fake_run(argv, **kwargs):
                if argv[0] == "dpkg-query":
                    installed = argv[-1] in ("git", "cmake")
                    return subprocess.CompletedProcess(argv, 0, "install ok installed 1.0" if installed else "", "")
                return subprocess.run(argv, **kwargs)
            with mock.patch.object(pv.subprocess, "run", side_effect=fake_run):
                with self.assertRaises(pv.NeedsApt) as caught:
                    pv.step_preflight(p)
        commands = caught.exception.commands
        self.assertTrue(commands[0].startswith("sudo apt-get install -y "))
        self.assertNotIn(" git ", commands[0] + " ")
        llvm = pv.PINS["platform"]["llvm"]
        self.assertIn(f"clang-21={llvm['version']}", commands[-1])
        self.assertTrue(any("apt.llvm.org/noble/ llvm-toolchain-noble-21" in c for c in commands))

    def test_paths_with_spaces_are_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = pv.Provisioner(Path(d) / "with space" / "deps", 2, None, None, quiet=True)
            with self.assertRaises(pv.StepError):
                pv.step_preflight(p)


class ToolSources(unittest.TestCase):
    def test_a_wrong_version_of_your_own_folder_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            fake = Path(d) / "rexglue"
            fake.mkdir()
            p = pv.Provisioner(Path(d) / "deps", 2, fake, None, quiet=True)
            with mock.patch.object(pv.Provisioner, "git_fetch"):
                with self.assertRaises(pv.StepError) as caught:
                    pv.step_tool_sources(p)
        self.assertIn("Setup fetches the right one", str(caught.exception))

    def test_line_changes_keep_the_pinned_line_endings(self):
        with tempfile.TemporaryDirectory() as d:
            dest = Path(d)
            (dest / "src").mkdir()
            (dest / "src/a.cpp").write_bytes(b"one\ntwo\n")
            change = dict(role="r", path="src/a.cpp", old="two\n", new="TWO\n", line_endings="crlf")
            with mock.patch.dict(pv.PINS, {"source_transforms": [change], "source_wraps": []}):
                pv.prepare_source(None, "r", dest)
                pv.prepare_source(None, "r", dest)  # running again changes nothing
            self.assertEqual((dest / "src/a.cpp").read_bytes(), b"one\r\nTWO\r\n")


if __name__ == "__main__":
    unittest.main()
