"""Installer checks with a fake folder and a fake console (no PS5, no network):

    python -B tests/test_installer.py
"""
import hashlib, io, json, os, shutil, sys, tempfile, unittest, urllib.error, urllib.request, zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from installer import catalog, editor, engine, helper, ps5x360, release, server, targets, toolchain

PARAM = {"titleId": "X", "localizedParameters": {"defaultLanguage": "en-US", "en-US": {"titleName": "Old name"}}}


def write(path: Path, data=b"x"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())
    return path


def fake_ps5x360(path: Path, extra=None):
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("PPSA50011/LICENSE", "MIT License\nCopyright BrinooTk\n")
        z.writestr("PPSA50011/eboot.bin", b"emulator")
        z.writestr("PPSA50011/sce_sys/param.json", "{}")
        for name, data in (extra or {}).items():
            z.writestr(name, data)
    return path


def fake_folder(root: Path, full=True) -> Path:
    """A stand-in for a user's folder. Every file is made up; nothing here is real Xbox content."""
    write(root / "PPSA99735/eboot.bin", b"dashboard-self")
    write(root / "PPSA99735/sce_sys/param.json", json.dumps(dict(PARAM, titleId="PPSA99735")))
    if not full:
        return root
    write(root / "dashboard-resources/NXE/Retail/9199/dash.xex/dashnxe.xzp", b"XUIZ fake nxe")
    write(root / "dashboard-resources/Metro/V1/Retail/15574/dash.xex/dashmetro.xzp", b"XUIZ fake metro")
    write(root / "shrdres.xzp", b"XUIZ shared")
    write(root / "avatar/kit360.nxak", b"NXAK\x02\x00\x00\x00 fake kit")
    write(root / "fonts/MyFont.ttf", b"\x00\x01\x00\x00 font")
    write(root / "trailers/manifest.tsv", "a\tA trailer\n")
    write(root / "trailers/packed/a.frames", b"frames")
    write(root / "trailers/packed/a.wav", b"RIFF....WAVE")
    write(root / "music/Album/song.mp3", b"ID3 song")
    write(root / "games/My Game/default.xex", b"XEX2 game")
    write(root / "helper/exbox360-daemon.elf", b"\x7fELF helper built by the user")
    write(root / "loading.png", b"\x89PNG\r\n\x1a\n fake picture")
    write(root / "PPSA99734/eboot.bin", b"editor-self")
    write(root / "PPSA99734/sce_sys/param.json", json.dumps(dict(PARAM, titleId="PPSA99734")))
    write(root / "PPSA99734/assets/ui/loading-1080p.png", b"png")
    write(root / "private/ps5/background.png", b"bg")
    write(root / "private/xbox/shrdres/A-Button.png", b"a")
    write(root / "private/era-library/15574/dash/tile.png", b"tile")
    write(root / "old/avatareditor.xex", b"XEX2 not the supported editor")
    return root


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        os.environ["EXBOX_SETUP_HOME"] = str(self.tmp / "home")
        self.folder = self.tmp / "my files"
        self.console = targets.LocalTarget(self.tmp / "console")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        zip_path = fake_ps5x360(self.tmp / "release" / "x.zip") if (self.tmp / "release").mkdir() is None else None
        self._pin = (ps5x360.SHA256, ps5x360.SIZE)
        ps5x360.SHA256 = hashlib.sha256(zip_path.read_bytes()).hexdigest()
        ps5x360.SIZE = zip_path.stat().st_size
        self.release = zip_path
        self.addCleanup(lambda: (setattr(ps5x360, "SHA256", self._pin[0]), setattr(ps5x360, "SIZE", self._pin[1])))

    def install(self, **options):
        result = catalog.scan(self.folder)
        return engine.Installer(self.console, result, options, log=None).run()

    def on_console(self, path):
        return self.console.read(path)


class Scan(Base):
    def test_full_folder_checklist(self):
        fake_folder(self.folder)
        shutil.copyfile(self.release, self.folder / "PPSA50011.zip")
        result = catalog.scan(self.folder)
        status = {item["id"]: item["status"] for item in result["items"]}
        for item_id in ("dashboard-app", "era-nxe", "era-metro", "shared-art", "fonts", "avatar-kit", "editor",
                        "icons-art", "trailers", "music", "ps5x360", "games360", "companion"):
            self.assertEqual(status[item_id], "found", item_id)
        for item_id in ("era-blades", "era-blades2", "era-kinect", "era-metro2"):
            self.assertEqual(status[item_id], "missing", item_id)
        self.assertTrue(any("avatareditor.xex" in line for line in result["wrong"]))
        self.assertIn("NXE (2008)", " ".join(result["preview"]["get"]))
        self.assertIn("Kinect (2010)", " ".join(result["preview"]["skip"]))
        self.assertTrue(result["preview"]["can_install"])
        self.assertEqual(result["sources"]["editor_mode"], "package")

    def test_every_line_says_what_it_unlocks_and_where_it_comes_from(self):
        self.folder.mkdir()
        result = catalog.scan(self.folder)
        for item in result["items"]:
            for key in ("title", "unlocks", "missing", "source"):
                self.assertGreater(len(item[key]), 4, (item["id"], key))
            self.assertNotIn("http", item["source"] if item["id"] != "ps5x360" else "")
        status = {item["id"]: item["status"] for item in result["items"]}
        self.assertEqual(status.pop("ps5x360"), "download")
        self.assertEqual(set(status.values()), {"missing"})
        self.assertFalse(result["preview"]["can_install"])

    def test_scan_changes_nothing(self):
        fake_folder(self.folder)
        before = sorted((str(p), p.stat().st_size, p.stat().st_mtime_ns) for p in self.folder.rglob("*"))
        catalog.scan(self.folder)
        self.assertEqual(before, sorted((str(p), p.stat().st_size, p.stat().st_mtime_ns)
                                        for p in self.folder.rglob("*")))

    def test_editor_originals_without_tools_is_partial(self):
        pins = catalog._pins()
        self.folder.mkdir()
        data = b"XEX2 pretend editor"
        write(self.folder / "avatareditor.xex", data)
        real = catalog._pins
        catalog._pins = lambda: dict(pins, editor_xex=hashlib.sha256(data).hexdigest())
        try:
            result = catalog.scan(self.folder)
        finally:
            catalog._pins = real
        item = next(i for i in result["items"] if i["id"] == "editor")
        self.assertEqual(item["status"], "partial")
        self.assertIn("xenonclatin.xtt", item["detail"])
        self.assertNotIn("ReXGlue", item["detail"])  # Setup fetches it; only the originals are asked for

    def test_wrong_ps5x360_file_is_rejected(self):
        self.folder.mkdir()
        write(self.folder / "PPSA50011.zip", b"PK not the release")
        result = catalog.scan(self.folder)
        self.assertIsNone(result["sources"]["ps5x360_zip"])
        self.assertTrue(any("PPSA50011.zip" in line for line in result["wrong"]))

    def test_missing_folder_is_a_plain_message(self):
        with self.assertRaises(ValueError):
            catalog.scan(self.tmp / "nope")


class Install(Base):
    def test_one_button_install_end_to_end(self):
        fake_folder(self.folder)
        shutil.copyfile(self.release, self.folder / "PPSA50011.zip")
        outcome = self.install()
        self.assertTrue(outcome["ok"], outcome)
        self.assertEqual([s["status"] for s in outcome["steps"]], ["done"] * 12, outcome["steps"])
        self.assertEqual(outcome["steps"][-1]["id"], "branding")  # names and icons are the last step
        self.assertEqual(self.on_console("/data/homebrew/PPSA99735/eboot.bin"), b"dashboard-self")
        self.assertEqual(self.on_console("/data/homebrew/PPSA99734/eboot.bin"), b"editor-self")
        self.assertEqual(self.on_console("/data/homebrew/PPSA50011/eboot.bin"), b"emulator")
        self.assertIn(b"MIT", self.on_console("/data/homebrew/PPSA50011/LICENSE"))
        self.assertIsNotNone(self.on_console("/data/homebrew/ExBox360/private_art/avatar/kit360.nxak"))
        self.assertIsNotNone(self.on_console("/data/homebrew/ExBox360/private_art/fonts/MyFont.ttf"))
        self.assertIsNotNone(self.on_console("/data/homebrew/ExBox360/private_art/background.png"))
        self.assertIsNotNone(self.on_console("/data/homebrew/ExBox360/private_library/eras/15574/dash/tile.png"))
        self.assertIsNotNone(self.on_console("/data/homebrew/ExBox360/trailers/packed/a.frames"))
        self.assertIsNotNone(self.on_console("/data/nxe/music/song.mp3"))
        self.assertIsNotNone(self.on_console("/data/pldmgr/payloads/ExBox360-Daemon/exbox360-daemon.elf"))
        self.assertEqual(self.on_console("/data/pldmgr/autoload.txt"), b"exbox360-daemon.elf\n")
        names = {t: json.loads(self.on_console(f"/data/homebrew/{t}/sce_sys/param.json"))
                 ["localizedParameters"]["en-US"]["titleName"] for t in ("PPSA99735", "PPSA99734")}
        self.assertEqual(names, {"PPSA99735": "Ex-Box 360", "PPSA99734": "Ex-Box Avatars"})
        self.assertIsNotNone(self.on_console("/data/homebrew/PPSA99735/sce_sys/icon0.png"))
        record = json.loads(self.on_console("/data/nxe/install-record.json"))
        self.assertEqual(record["components"]["ps5x360"]["licence"], "MIT")
        self.assertIn("avatar-kit", record["unlocked"])
        text = json.dumps(record)
        self.assertNotIn(str(self.tmp), text)  # names, sizes and fingerprints only; no PC paths
        self.assertNotIn(str(self.tmp).replace("\\", "\\\\"), text)
        self.assertTrue(Path(outcome["log_file"]).is_file())
        self.assertTrue(engine.doctor(self.console)["ok"])
        # games are counted, never copied
        self.assertFalse(any("default.xex" in str(p) for p in (self.tmp / "console").rglob("*")))

    def test_missing_optional_pieces_are_skipped_not_errors(self):
        fake_folder(self.folder, full=False)
        outcome = self.install()
        self.assertTrue(outcome["ok"], outcome)
        status = {s["id"]: s["status"] for s in outcome["steps"]}
        self.assertEqual(status["dashboard"], "done")
        for step in ("art", "kit", "fonts", "trailers", "music", "editor", "companion"):
            self.assertEqual(status[step], "skipped", step)
        self.assertEqual(status["ps5x360"], "simulated")  # practice runs never download
        self.assertIsNone(self.on_console("/data/pldmgr/autoload.txt"))
        for step in outcome["steps"]:
            self.assertTrue(step["text"], step)

    def test_second_run_only_installs_what_changed_and_restore_undoes_it(self):
        fake_folder(self.folder)
        self.install()
        write(self.folder / "avatar/kit360.nxak", b"NXAK\x02\x00\x00\x00 a newer kit")
        write(self.folder / "music/Album/second.mp3", b"ID3 two")
        result = catalog.scan(self.folder)
        news = catalog.new_since(result, engine.load_record(self.console))
        self.assertTrue(any("Music" in line for line in news), news)
        self.assertTrue(any("Avatar kit" in line for line in news), news)
        installer = engine.Installer(self.console, result, log=None)
        installer.run_id = "29990101T000000Z"
        installer.work = engine.home() / "runs" / installer.run_id
        outcome = installer.run()
        self.assertTrue(outcome["ok"], outcome)
        kit = "/data/homebrew/ExBox360/private_art/avatar/kit360.nxak"
        self.assertEqual(self.on_console(kit), b"NXAK\x02\x00\x00\x00 a newer kit")
        self.assertEqual(self.on_console(f"/data/nxe/install-backups/{installer.run_id}/original{kit}"),
                         b"NXAK\x02\x00\x00\x00 fake kit")
        replaced = [e["path"] for e in installer.journal if e["action"] == "replaced"]
        self.assertIn(kit, replaced)
        self.assertNotIn("/data/homebrew/ExBox360/private_art/background.png", replaced)
        self.assertNotIn("/data/homebrew/PPSA99735/eboot.bin", replaced)
        self.assertEqual(sum(1 for e in installer.journal if e["path"].endswith("sce_sys/param.json")), 2,
                         "only the names step touches param.json on a repeat run")
        report = engine.restore(self.console, log=lambda *_: None)
        self.assertTrue(report["ok"], report)
        self.assertEqual(self.on_console(kit), b"NXAK\x02\x00\x00\x00 fake kit")
        self.assertIsNone(self.on_console("/data/nxe/music/second.mp3"))

    def test_restore_after_first_install_removes_what_was_added(self):
        fake_folder(self.folder)
        write(self.tmp / "console/data/pldmgr/autoload.txt", b"other.elf\r\n/data/pldmgr/payloads/ExBox360-Daemon/exbox360-daemon.elf\r\n")
        self.install()
        self.assertEqual(self.on_console("/data/pldmgr/autoload.txt"), b"other.elf\r\nexbox360-daemon.elf\r\n")
        engine.restore(self.console, log=lambda *_: None)
        self.assertIsNone(self.on_console("/data/homebrew/PPSA99735/eboot.bin"))
        self.assertEqual(self.on_console("/data/pldmgr/autoload.txt"),
                         b"other.elf\r\n/data/pldmgr/payloads/ExBox360-Daemon/exbox360-daemon.elf\r\n")
        self.assertFalse(engine.restore(self.console)["ok"])  # nothing left to restore

    def test_doctor_notices_a_missing_file(self):
        fake_folder(self.folder)
        self.install()
        (self.tmp / "console/data/homebrew/ExBox360/private_art/background.png").unlink()
        report = engine.doctor(self.console)
        self.assertFalse(report["ok"])
        self.assertTrue(any("missing" in line for line in report["lines"]))

    def test_dashboard_already_on_console_is_kept(self):
        self.folder.mkdir()
        write(self.folder / "music/song.mp3", b"ID3 song")
        write(self.tmp / "console/data/homebrew/PPSA99735/eboot.bin", b"already here")
        outcome = self.install()
        self.assertTrue(outcome["ok"], outcome)
        self.assertIn("Already on your PS5", outcome["steps"][1]["text"])
        self.assertEqual(self.on_console("/data/homebrew/PPSA99735/eboot.bin"), b"already here")

    def test_a_failed_upload_leaves_the_old_file(self):
        fake_folder(self.folder, full=False)
        self.install()
        write(self.folder / "PPSA99735/eboot.bin", b"dashboard-self-v2")
        real = self.console.sha256
        self.console.sha256 = lambda path: "0" * 64 if path.endswith(".exbox-new") else real(path)
        outcome = self.install()
        self.assertFalse(outcome["ok"])
        self.assertIn("Restore", outcome["summary"])
        self.assertEqual(self.on_console("/data/homebrew/PPSA99735/eboot.bin"), b"dashboard-self")
        self.assertIsNone(self.on_console("/data/homebrew/PPSA99735/eboot.bin.exbox-new"))


class Helper(Base):
    """The launch helper: Setup shows how to build it, then installs the file the user built."""

    def _source(self, built=False):
        src = self.folder / "launch-helper"
        write(src / "Makefile", "all: build/nxe_dashboard_daemon.elf\n\tprospero-clang -o $@ main.c\n")
        write(src / "main.c", "int main(void){return 0;}\n")
        write(src / "gpl/elfldr.c", "/* loader source, never read or shipped by Setup */\n")
        if built:
            write(src / "build/nxe_dashboard_daemon.elf", b"\x7fELF the user's own build")
        return src

    def test_source_only_asks_the_user_to_build_it(self):
        fake_folder(self.folder, full=False)
        self._source(built=False)
        result = catalog.scan(self.folder)
        item = next(i for i in result["items"] if i["id"] == "companion")
        self.assertEqual(item["status"], "build")
        self.assertEqual(item["build"]["commands"][-1], "make")
        self.assertTrue(item["build"]["folder"].endswith("launch-helper"))
        self.assertIn("build the source", " ".join(result["preview"]["skip"]))

    def test_setup_does_not_build_it_and_skips_with_a_plain_reason(self):
        fake_folder(self.folder, full=False)
        self._source(built=False)
        outcome = self.install()
        self.assertTrue(outcome["ok"], outcome)  # a missing optional piece is not an error
        step = next(s for s in outcome["steps"] if s["id"] == "companion")
        self.assertEqual(step["status"], "skipped")
        self.assertIn("Build it on this PC", step["text"])
        # Setup compiled nothing: no helper, no autoload line, no build output appeared
        self.assertIsNone(self.on_console("/data/pldmgr/payloads/ExBox360-Daemon/exbox360-daemon.elf"))
        self.assertIsNone(self.on_console("/data/pldmgr/autoload.txt"))
        self.assertFalse((self.folder / "launch-helper/build/nxe_dashboard_daemon.elf").exists())

    def test_the_file_the_user_built_is_picked_up_and_installed(self):
        fake_folder(self.folder, full=False)
        self._source(built=True)  # as if the user has now run make
        result = catalog.scan(self.folder)
        item = next(i for i in result["items"] if i["id"] == "companion")
        self.assertEqual(item["status"], "found")
        self.assertIn("Built the launch helper from its source", item["detail"])
        outcome = engine.Installer(self.console, result, log=None).run()
        self.assertEqual(self.on_console("/data/pldmgr/payloads/ExBox360-Daemon/exbox360-daemon.elf"),
                         b"\x7fELF the user's own build")
        self.assertEqual(self.on_console("/data/pldmgr/autoload.txt"), b"exbox360-daemon.elf\n")

    def test_a_prebuilt_helper_elf_still_works(self):
        fake_folder(self.folder, full=False)
        write(self.folder / "helper/exbox360-daemon.elf", b"\x7fELF prebuilt by the user")
        item = next(i for i in catalog.scan(self.folder)["items"] if i["id"] == "companion")
        self.assertEqual(item["status"], "found")
        self.assertIn("Found the launch helper you built", item["detail"])

    def test_helper_source_shipped_with_exbox_is_used_when_the_folder_has_none(self):
        # The owner has added the helper source to Ex-Box's own launch-helper/ folder.
        bundled = self.tmp / "exbox-launch-helper"
        write(bundled / "Makefile", "all: build/nxe_dashboard_daemon.elf\n\tprospero-clang -o $@ main.c\n")
        write(bundled / "main.c", "int main(void){return 0;}\n")
        real = helper.BUNDLED
        helper.BUNDLED = bundled
        try:
            fake_folder(self.folder, full=False)  # the user's own folder has no helper at all
            item = next(i for i in catalog.scan(self.folder)["items"] if i["id"] == "companion")
            self.assertEqual(item["status"], "build")
            self.assertTrue(item["build"]["folder"].endswith("exbox-launch-helper"))
            # once the owner/user builds it, Setup installs it from the shipped folder
            write(bundled / "build/nxe_dashboard_daemon.elf", b"\x7fELF built from the shipped source")
            result = catalog.scan(self.folder)
            self.assertEqual(next(i for i in result["items"] if i["id"] == "companion")["status"], "found")
            outcome = engine.Installer(self.console, result, log=None).run()
            self.assertEqual(self.on_console("/data/pldmgr/payloads/ExBox360-Daemon/exbox360-daemon.elf"),
                             b"\x7fELF built from the shipped source")
        finally:
            helper.BUNDLED = real


class Dashboard(Base):
    """The Ex-Box dashboard app: bundled with Setup, or fetched from the pinned Ex-Box release."""

    def _dash_zip(self, path):
        with zipfile.ZipFile(path, "w") as z:
            z.writestr("PPSA99735/eboot.bin", b"\x7fELF dashboard from the release")
            z.writestr("PPSA99735/sce_sys/param.json", json.dumps(dict(PARAM, titleId="PPSA99735")))
        return path

    def _scan(self, payload=None):
        return catalog.scan(self.folder, package_dirs=[payload] if payload else None)

    def test_bundled_payload_is_used_with_no_network(self):
        self.folder.mkdir()
        write(self.folder / "music/song.mp3", b"ID3 song")
        payload = self.tmp / "payload"
        write(payload / "PPSA99735/eboot.bin", b"\x7fELF our dashboard")
        write(payload / "PPSA99735/sce_sys/param.json", json.dumps(dict(PARAM, titleId="PPSA99735")))
        real = release.PAYLOAD
        release.PAYLOAD = payload
        try:
            result = catalog.scan(self.folder)
            item = next(i for i in result["items"] if i["id"] == "dashboard-app")
            self.assertEqual(item["status"], "found")
            self.assertTrue(result["preview"]["can_install"])
            outcome = engine.Installer(self.console, result, log=None).run()
            self.assertEqual(self.on_console("/data/homebrew/PPSA99735/eboot.bin"), b"\x7fELF our dashboard")
        finally:
            release.PAYLOAD = real

    def test_pinned_release_is_fetched_verified_and_installed(self):
        self.folder.mkdir()
        write(self.folder / "music/song.mp3", b"ID3 song")
        zip_path = self._dash_zip(self.tmp / "ExBox360-PPSA99735.zip")
        sha = hashlib.sha256(zip_path.read_bytes()).hexdigest()
        real = release.RELEASE
        release.RELEASE = dict(tag="v1.0", asset="ExBox360-PPSA99735.zip", size=zip_path.stat().st_size, sha256=sha)
        try:
            result = catalog.scan(self.folder)
            item = next(i for i in result["items"] if i["id"] == "dashboard-app")
            self.assertEqual(item["status"], "download")
            self.assertIn("v1.0", item["detail"])
            self.assertTrue(result["preview"]["can_install"])

            class Reply(io.BytesIO):
                def __enter__(self): return self
                def __exit__(self, *_): return False
            installer = engine.Installer(self.console, result, dict(allow_download=True, ps5x360=False), log=None)
            # feed the fetch a fake network response instead of a real download
            orig = release.fetch
            release.fetch = lambda cache, progress=None: orig(cache, progress,
                                                               opener=lambda req, timeout: Reply(zip_path.read_bytes()))
            try:
                outcome = installer.run()
            finally:
                release.fetch = orig
            self.assertTrue(outcome["ok"], outcome)
            self.assertEqual(self.on_console("/data/homebrew/PPSA99735/eboot.bin"),
                             b"\x7fELF dashboard from the release")
        finally:
            release.RELEASE = real

    def test_practice_run_does_not_download_the_dashboard(self):
        self.folder.mkdir()
        write(self.folder / "music/song.mp3", b"ID3 song")
        real = release.RELEASE
        release.RELEASE = dict(tag="v1.0", asset="x.zip", size=10, sha256="0" * 64)
        try:
            result = catalog.scan(self.folder)
            outcome = engine.Installer(self.console, result, log=None).run()  # practice: allow_download False
            step = next(s for s in outcome["steps"] if s["id"] == "dashboard")
            self.assertEqual(step["status"], "simulated")
            self.assertIsNone(self.on_console("/data/homebrew/PPSA99735/eboot.bin"))
        finally:
            release.RELEASE = real


class Tools(Base):
    """Self-provisioned build tools and the shared libc.prx, with the WSL side faked."""

    def _editor_folder(self):
        data = {"avatareditor.xex": b"XEX2 pretend editor", "xenonclatin.xtt": b"pretend font",
                "splash_360.png": b"\x89PNG\r\n\x1a\n pretend splash"}
        for name, content in data.items():
            write(self.folder / "originals" / name, content)
        pins = catalog._pins()
        fake = dict(pins, editor_xex=hashlib.sha256(data["avatareditor.xex"]).hexdigest(),
                    font_xtt=hashlib.sha256(data["xenonclatin.xtt"]).hexdigest(),
                    splash=hashlib.sha256(data["splash_360.png"]).hexdigest())
        real = catalog._pins
        catalog._pins = lambda: fake
        self.addCleanup(setattr, catalog, "_pins", real)

    def _fake_wsl(self, status="ready"):
        calls = {}
        deps = "/home/user/.local/share/exbox-toolchain/dependencies"
        def provision(rexglue=None, xex=None, progress=None, log=None, **_):
            calls.setdefault("provision", []).append((rexglue, xex))
            if progress:
                progress(0.5, "Building SPIR-V tools")
            return dict(status=status, dependencies=deps, commands=["sudo apt-get install -y spirv-tools"])
        def build_libc(tools, out_dir):
            calls["libc"] = tools
            path = out_dir / "libc-a.prx"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"\x7fELF libc built here")
            return path
        def build(originals, dependencies, work, log=None, progress=None, linux_work=None, **_):
            calls["editor"] = (dependencies, linux_work)
            package = work.parent / "editor.zip"
            with zipfile.ZipFile(package, "w") as z:
                z.writestr("PPSA99734/eboot.bin", b"editor built from your files")
                z.writestr("PPSA99734/sce_module/libc.prx", b"\x7fELF libc built here")
            return package
        for module, name, value in ((toolchain, "provision", provision), (toolchain, "build_libc", build_libc),
                                    (editor, "build", build)):
            real = getattr(module, name)
            setattr(module, name, value)
            self.addCleanup(setattr, module, name, real)
        return calls

    def test_scan_offers_the_set_up_and_build_path(self):
        self._editor_folder()
        result = catalog.scan(self.folder)
        item = next(i for i in result["items"] if i["id"] == "editor")
        self.assertEqual(item["status"], "found")
        self.assertEqual(result["sources"]["editor_mode"], "provision")

    def test_practice_run_describes_but_does_not_set_up(self):
        self._editor_folder()
        calls = self._fake_wsl()
        outcome = self.install()
        step = next(s for s in outcome["steps"] if s["id"] == "editor")
        self.assertEqual(step["status"], "simulated")
        self.assertIn("set up the build tools", step["text"])
        self.assertEqual(calls, {})

    def test_real_run_sets_up_tools_builds_editor_and_shares_libc(self):
        fake_folder(self.folder, full=False)  # dashboard package without libc.prx
        self._editor_folder()
        calls = self._fake_wsl()
        result = catalog.scan(self.folder)
        outcome = engine.Installer(self.console, result, dict(build_editor=True, ps5x360=False), log=None).run()
        self.assertTrue(outcome["ok"], outcome)
        self.assertIn("libc.prx was built on this PC", next(s for s in outcome["steps"] if s["id"] == "dashboard")["text"])
        self.assertEqual(self.on_console("/data/homebrew/PPSA99735/sce_module/libc.prx"), b"\x7fELF libc built here")
        self.assertEqual(self.on_console("/data/homebrew/PPSA99734/sce_module/libc.prx"), b"\x7fELF libc built here")
        self.assertEqual(self.on_console("/data/homebrew/PPSA99734/eboot.bin"), b"editor built from your files")
        self.assertTrue(calls["editor"][1].endswith("/editor-builds/" + outcome["run"]))
        self.assertEqual(calls["provision"], [(None, None)], "tools set up once; Setup fetches ReXGlue and XexTool-RE itself")

    def test_libc_already_on_the_ps5_is_kept(self):
        fake_folder(self.folder, full=False)
        write(self.tmp / "console/data/homebrew/PPSA99735/sce_module/libc.prx", b"already there")
        calls = self._fake_wsl()
        outcome = engine.Installer(self.console, catalog.scan(self.folder), dict(build_editor=True, ps5x360=False),
                                   log=None).run()
        self.assertIn("Kept the libc.prx", next(s for s in outcome["steps"] if s["id"] == "dashboard")["text"])
        self.assertNotIn("libc", calls)

    def test_tool_setup_problems_are_explained_plainly(self):
        self._editor_folder()
        self._fake_wsl(status="needs-apt")
        outcome = engine.Installer(self.console, catalog.scan(self.folder), dict(build_editor=True, ps5x360=False),
                                   log=None).run()
        step = next(s for s in outcome["steps"] if s["id"] == "editor")
        self.assertEqual(step["status"], "failed")
        self.assertIn("sudo apt-get install -y spirv-tools", step["text"])

    def test_progress_and_result_lines_are_read(self):
        lines = ["PROGRESS 2 16 sources Fetching the pinned public sources\n", "plain log line\n",
                 'RESULT {"status": "ready", "dependencies": "/home/u/deps"}\n']
        class Fake:
            def __init__(self, argv, **_):
                self.argv, self.stdout = argv, iter(lines)
            def wait(self):
                return 0
        seen, logged = [], []
        result = toolchain.provision(rexglue=self.tmp / "r", progress=lambda f, t: seen.append((round(f, 3), t)),
                                     log=logged.append, runner=Fake)
        self.assertEqual(result["status"], "ready")
        self.assertEqual(seen, [(0.125, "Fetching the pinned public sources")])
        self.assertEqual(logged, ["plain log line"])

    def test_wsl_paths(self):
        self.assertEqual(toolchain.wsl("/home/u/x"), "/home/u/x")
        self.assertEqual(editor._wsl("/home/u/x"), "/home/u/x")


class SeamlessEditor(Base):
    """Owner decision 2026-11-11: Setup fetches ReXGlue and XexTool-RE itself; only the three originals are asked for."""

    def test_no_bring_your_own_lines(self):
        self.folder.mkdir()
        items = {i["id"]: i for i in catalog.scan(self.folder)["items"]}
        self.assertNotIn("rexglue", items)
        self.assertNotIn("xex-tool", items)
        self.assertIn("ReXGlue and XexTool-RE", items["editor"]["source"])
        self.assertIn("avatareditor.xex", items["editor"]["place"])
        self.assertIsNone(items["editor"]["link"])

    def test_three_originals_are_enough(self):
        data = {"avatareditor.xex": b"XEX2 pretend editor", "xenonclatin.xtt": b"pretend font",
                "splash_360.png": b"\x89PNG\r\n\x1a\n pretend splash"}
        for name, content in data.items():
            write(self.folder / name, content)
        pins = catalog._pins()
        fake = dict(pins, editor_xex=hashlib.sha256(data["avatareditor.xex"]).hexdigest(),
                    font_xtt=hashlib.sha256(data["xenonclatin.xtt"]).hexdigest(),
                    splash=hashlib.sha256(data["splash_360.png"]).hexdigest())
        real = catalog._pins
        catalog._pins = lambda: fake
        self.addCleanup(setattr, catalog, "_pins", real)
        result = catalog.scan(self.folder)
        self.assertEqual(result["sources"]["editor_mode"], "provision")

    def test_the_project_page_endpoint_is_gone(self):
        with self.assertRaises(ValueError):
            server.api(server.State(), "open-link", dict(id="rexglue"))

class Safety(Base):
    def test_only_owned_paths_can_be_written(self):
        for path in ("/data/homebrew/PPSA99735/eboot.bin", "/data/nxe/x", "/data/pldmgr/autoload.txt",
                     "/data/pldmgr/payloads/ExBox360-Daemon/a.elf", "/system_data/priv/mms/app.db.pre-branding-1"):
            targets.check_write(path)
        for path in ("/data/homebrew/OtherGame/eboot.bin", "/data/pldmgr/other.elf", "/data/nxe/../etc/x",
                     "/system_data/priv/mms/other.db", "relative/path", "/user/appmeta/CUSA00001/param.json",
                     "/data/homebrew/PPSA99735x/eboot.bin"):
            with self.assertRaises(targets.NotAllowed, msg=path):
                targets.check_write(path)
        with self.assertRaises(targets.NotAllowed):
            self.console.upload("/data/homebrew/OtherGame/eboot.bin", io.BytesIO(b"x"))

    def test_autoload_line(self):
        fix = engine.fix_autoload
        self.assertEqual(fix(None, "exbox360-daemon.elf"), b"exbox360-daemon.elf\n")
        self.assertEqual(fix(b"a.elf\nrepo/x@abc\n", "exbox360-daemon.elf"), b"a.elf\nrepo/x@abc\nexbox360-daemon.elf\n")
        self.assertEqual(fix(b"a.elf\r\n/data/pldmgr/exbox360-daemon.elf\r\nb.elf\r\n", "exbox360-daemon.elf"),
                         b"a.elf\r\nb.elf\r\nexbox360-daemon.elf\r\n")
        once = fix(b"a.elf\nnxe_dashboard_daemon.elf\n", "exbox360-daemon.elf")
        self.assertEqual(once, b"a.elf\nexbox360-daemon.elf\n")
        self.assertEqual(fix(once, "exbox360-daemon.elf"), once)
        self.assertEqual(fix(b"/somewhere/else/other-daemon.elf\n", "exbox360-daemon.elf"),
                         b"/somewhere/else/other-daemon.elf\nexbox360-daemon.elf\n")

    def test_ps5x360_pin_and_unsafe_archives(self):
        ps5x360.check(self.release)
        tampered = fake_ps5x360(self.tmp / "tampered.zip", {"PPSA50011/extra.bin": b"x"})
        with self.assertRaises(ValueError):
            ps5x360.check(tampered)
        for name in ("../escape.txt", "OTHER/eboot.bin", "PPSA50011/../../x"):
            bad = fake_ps5x360(self.tmp / "bad.zip", {name: b"x"})
            with self.assertRaises(ValueError, msg=name):
                list(ps5x360.members(bad))
        self.assertIn("releases/download/" + ps5x360.VERSION, ps5x360.URL)
        self.assertTrue(ps5x360.URL.startswith("https://github.com/BrinooTk/PS5X360/"))

    def test_ps5x360_download_is_checked(self):
        class Reply(io.BytesIO):
            def __enter__(self): return self
            def __exit__(self, *_): return False
        good = ps5x360.fetch(self.tmp / "dl", opener=lambda request, timeout: Reply(self.release.read_bytes()))
        self.assertEqual(good.read_bytes(), self.release.read_bytes())
        with self.assertRaises(ValueError):
            ps5x360.fetch(self.tmp / "dl2", opener=lambda request, timeout: Reply(b"x" * ps5x360.SIZE))
        self.assertEqual(list((self.tmp / "dl2").iterdir()), [])

    def test_real_pin_is_the_official_release(self):
        self.assertEqual(self._pin, ("230359c2638f99add3972147e1690e62d8984a37c00181501680bdda7c1096c8", 86415109))

    def test_editor_package_zip_layout(self):
        package = self.tmp / "Editor-Test.zip"
        with zipfile.ZipFile(package, "w") as z:
            z.writestr("package-result.json", "{}")
            z.writestr("PPSA99734/eboot.bin", b"e")
            z.writestr("PPSA99734/assets/ui/a.png", b"p")
        files = editor.package_files(package, "/data/homebrew/PPSA99734", engine.Src)
        self.assertEqual(sorted(files), ["/data/homebrew/PPSA99734/assets/ui/a.png", "/data/homebrew/PPSA99734/eboot.bin"])
        command = editor.command(Path("a"), Path("b"), Path("c"))
        self.assertIn("--dependencies", command)
        self.assertTrue(any(part.endswith("builder.py") for part in command))


class Window(Base):
    def test_api_scan_and_install(self):
        fake_folder(self.folder)
        state = server.State()
        view = server.api(state, "scan", dict(folder=str(self.folder)))
        self.assertTrue(view["preview"]["can_install"])
        self.assertNotIn("sources", view)
        server.api(state, "install", dict(mode="practice", icons="default"))
        state.job.installer  # started
        for _ in range(600):
            if state.job.result is not None:
                break
            import time; time.sleep(0.05)
        self.assertTrue(state.job.result["ok"], state.job.result)
        self.assertTrue(server.api(state, "doctor", dict(mode="practice"))["ok"])
        with self.assertRaises(ValueError):
            server.api(state, "install", dict(mode="console", host=""))

    def test_page_needs_the_window_key(self):
        import threading
        from http.server import ThreadingHTTPServer
        state = server.State()
        httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.make_handler(state))
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        self.addCleanup(httpd.server_close)
        self.addCleanup(httpd.shutdown)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
        page = urllib.request.urlopen(base + "/").read().decode()
        self.assertIn(state.key, page)
        def post(key):
            request = urllib.request.Request(base + "/api/info", data=b"{}", headers={"X-ExBox-Key": key})
            try:
                return urllib.request.urlopen(request).status
            except urllib.error.HTTPError as error:
                error.close()
                return error.code
        self.assertEqual(post("wrong"), 403)
        self.assertEqual(post(state.key), 200)


if __name__ == "__main__":
    unittest.main()
