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
        for item in pv.PINS["sources"] + pv.PINS["git_tools"] + pv.PINS["user_supplied_sources"]:
            self.assertRegex(item["commit"], r"^[0-9a-f]{40}$", item)
        for item in pv.PINS["downloads"] + pv.PINS["python_wheels"]:
            self.assertRegex(item["sha256"], r"^[0-9a-f]{64}$", item)
        self.assertRegex(pv.PINS["native_sdk"]["base_release_sha256"], r"^[0-9a-f]{64}$")
        self.assertIn("2078da43e25a", pv.PINS["platform"]["llvm"]["version"])

    def test_every_source_is_an_official_upstream(self):
        urls = [i.get("git") or i.get("url") for i in pv.PINS["sources"] + pv.PINS["git_tools"] + pv.PINS["downloads"]]
        urls += [pv.PINS["native_sdk"]["base_release"], pv.PINS["platform"]["llvm"]["repo"]]
        for url in urls:
            self.assertTrue(url.startswith(OFFICIAL_HOSTS), url)
            self.assertNotIn("egomortal", url)  # nothing rehosted by Ex-Box

    def test_key_bearing_sources_are_never_fetched(self):
        fetched = {i["role"] for i in pv.PINS["sources"]}
        for item in pv.PINS["user_supplied_sources"]:
            self.assertNotIn(item["role"], fetched)
            self.assertIn("decryption key", item["why_not_fetched"])
        names = " ".join(json.dumps(i) for i in pv.PINS["sources"] + pv.PINS["git_tools"] + pv.PINS["downloads"])
        self.assertNotIn("rexglue", names.lower())
        self.assertNotIn("xextool", names.lower())

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


class UserSources(unittest.TestCase):
    def test_without_your_folders_setup_waits_and_says_why(self):
        with tempfile.TemporaryDirectory() as d:
            p = pv.Provisioner(Path(d) / "deps", 2, None, None, quiet=True)
            text = pv.step_user_sources(p)
        self.assertIn("decryption key", text)
        self.assertEqual({m["role"] for m in p.missing_user}, {"rexglue_sdk", "xex-tool"})

    def test_a_wrong_version_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            fake = Path(d) / "rexglue"
            fake.mkdir()
            p = pv.Provisioner(Path(d) / "deps", 2, fake, None, quiet=True)
            with self.assertRaises(pv.StepError) as caught:
                pv.step_user_sources(p)
        self.assertIn("c94f5ebd", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
