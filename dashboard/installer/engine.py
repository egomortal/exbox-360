"""The one-button install: plan, back up, install, verify, remember, and restore.

Order (INSTALLER-BRIEF section 2): preflight, dashboard, your art and media, Avatar Editor,
optional PS5X360, optional launch helper, install record, and names + icons last.
Anything optional that is missing is skipped with a plain reason; it is never an error.
Nothing on the console is replaced before the old copy has been moved into this run's backup folder.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
import subprocess
import sys
import time
import zipfile
from pathlib import Path

from . import VERSION, catalog, editor, ps5x360
from .targets import FtpLike, LocalTarget, Target

DASHBOARD = Path(__file__).resolve().parents[1]
OLD_APP = "/data/homebrew/ExBox360"
TITLES = {"dashboard": "/data/homebrew/PPSA99735", "editor": "/data/homebrew/PPSA99734",
          "ps5x360": "/data/homebrew/PPSA50011"}
RECORD = "/data/nxe/install-record.json"
BACKUPS = "/data/nxe/install-backups"
PAYLOAD_DIR = "/data/pldmgr/payloads/ExBox360-Daemon"
AUTOLOAD = "/data/pldmgr/autoload.txt"
SELF_SUFFIXES = (".elf", ".prx", ".sprx", ".self")
READBACK_LIMIT = 64 << 20
IMAGES = {".png", ".jpg", ".jpeg"}
BRANDED = ("/sce_sys/param.json", "/sce_sys/icon0.png")


def home() -> Path:
    """The installer's own app-data folder on this PC (records, backups copies, downloads, practice console)."""
    override = os.environ.get("EXBOX_SETUP_HOME")
    if override:
        path = Path(override)
    elif sys.platform == "win32":
        path = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local")) / "ExBox360-Setup"
    else:
        path = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "exbox360-setup"
    path.mkdir(parents=True, exist_ok=True)
    return path


def is_self(remote: str) -> bool:
    return remote.endswith("/eboot.bin") or remote.endswith(SELF_SUFFIXES)


# ---------- what gets uploaded ----------
class Src:
    """One file to install: from disk, from memory, or straight out of a ZIP without unpacking it."""

    def __init__(self, path: Path | None = None, data: bytes | None = None, zip_path: Path | None = None,
                 member: zipfile.ZipInfo | None = None):
        self.path, self.data, self.zip_path, self.member = path, data, zip_path, member
        self._sha = None
        if data is not None:
            self.size = len(data)
        elif member is not None:
            self.size = member.file_size
        else:
            self.size = path.stat().st_size

    def open(self):
        if self.data is not None:
            return io.BytesIO(self.data)
        if self.member is not None:
            archive = zipfile.ZipFile(self.zip_path)
            stream = archive.open(self.member)
            original_close = stream.close

            def close():
                original_close()
                archive.close()
            stream.close = close
            return stream
        return self.path.open("rb")

    def sha256(self, hasher: catalog.Hasher | None = None) -> str:
        if self._sha is None:
            if self.path is not None and hasher is not None:
                self._sha = hasher.sha256(self.path)
            else:
                digest = hashlib.sha256()
                with self.open() as stream:
                    for block in iter(lambda: stream.read(1 << 20), b""):
                        digest.update(block)
                self._sha = digest.hexdigest()
        return self._sha


def tree(folder: Path, remote_root: str, keep=lambda p: True) -> dict[str, Src]:
    return {f"{remote_root}/{p.relative_to(folder).as_posix()}": Src(p)
            for p in sorted(folder.rglob("*")) if p.is_file() and not p.is_symlink() and keep(p)}


def art_files(private: Path) -> dict[str, Src]:
    """Map an Ex-Box prepared-art tree to where the dashboard reads it (same layout deploy.py uses)."""
    files: dict[str, Src] = {}

    def add(remote, path):
        files[f"{OLD_APP}/{remote}"] = Src(path)

    def images(folder, remote):
        if folder.is_dir():
            for p in sorted(folder.rglob("*")):
                if p.is_file() and p.suffix.lower() in IMAGES:
                    add(f"{remote}/{p.relative_to(folder).as_posix()}", p)

    ps5 = private / "ps5"
    for name in ("A-Button.png", "B-Button.png", "X-Button.png", "Y-Button.png"):
        if (private / "xbox" / "shrdres" / name).is_file():
            add(f"private_buttons/{name}", private / "xbox" / "shrdres" / name)
    for p in sorted(ps5.glob("*.png")):
        add(f"private_art/{p.name}", p)
    for p in sorted((private / "game_art").glob("*")):
        if p.is_file() and p.suffix.lower() in {".jpg", ".png"}:
            add(f"game_art/{p.name}", p)
    for p in sorted((ps5 / "sounds").glob("*.wav")):
        add(f"private_art/sounds/{p.name}", p)
    eras = ps5 / "sounds" / "eras"
    if eras.is_dir():
        for p in sorted(eras.rglob("*")):
            if p.is_file():
                add(f"private_art/sounds/eras/{p.relative_to(eras).as_posix()}", p)
    for p in sorted((ps5 / "avatar").glob("avatar_*")):
        if p.suffix.lower() in {".frames", ".png"}:
            add(f"private_art/avatar/{p.name}", p)
    for p in sorted((ps5 / "fonts").glob("*")):
        if p.suffix.lower() in catalog.FONTS:
            add(f"private_art/fonts/{p.name}", p)
    for name in ("promotions", "era-skins"):
        folder = private / name
        if folder.is_dir():
            for p in sorted(folder.rglob("*")):
                if p.is_file():
                    add(f"{name}/{p.relative_to(folder).as_posix()}", p)
    images(private / "xbox" / "unpacked", "private_library")
    for build in ("6770", "13604", "15574", "17559"):
        images(private / "era-library" / build, f"private_library/eras/{build}")
    images(private / "xbox" / "shrdres", "private_library/shared")
    return files


def trailer_files(folder: Path) -> dict[str, Src]:
    files = {f"{OLD_APP}/trailers/manifest.tsv": Src(folder / "manifest.tsv")}
    for p in sorted((folder / "packed").glob("*")):
        if p.is_file() and p.suffix.lower() in {".frames", ".wav"}:
            files[f"{OLD_APP}/trailers/packed/{p.name}"] = Src(p)
    return files


# ---------- records ----------
def load_record(target: Target) -> dict | None:
    try:
        data = target.read(RECORD, limit=16 << 20)
        return json.loads(data) if data else None
    except (ValueError, OSError):
        return None


def pc_record_path(target: Target) -> Path:
    name = "practice" if target.practice else getattr(target, "host", "console").replace(":", "_")
    return home() / "records" / f"{name}.json"


class Cancelled(Exception):
    pass


class Installer:
    """options: icons ('xbox'|'default'), ps5x360 (bool), companion (bool), allow_download (bool),
    build_editor (bool), prepare_art (bool)."""

    def __init__(self, target: Target, scan: dict, options: dict | None = None, log=print, progress=None):
        self.target, self.scan, self.src = target, scan, scan["sources"]
        self.options = dict(icons="xbox", ps5x360=True, companion=True,
                            allow_download=not target.practice, build_editor=not target.practice,
                            prepare_art=True)
        self.options.update(options or {})
        self.log_lines: list[str] = []
        self._log, self._progress = log, progress
        self.run_id = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
        self.work = home() / "runs" / self.run_id
        self.journal: list[dict] = []
        self.files: dict[str, dict] = {}
        self.steps: list[dict] = []
        self.previous = load_record(target)
        self.hasher = catalog.Hasher(home() / "hash-cache.json")
        self.cancel = False
        self.restart_needed = False

    # ----- reporting -----
    def log(self, text: str):
        self.log_lines.append(text)
        if self._log:
            self._log(text)

    def tick(self, index, total, title, fraction=0.0, detail=""):
        if self.cancel:
            raise Cancelled()
        if self._progress:
            self._progress(dict(step=index, steps=total, title=title, fraction=fraction, detail=detail))

    # ----- backup-first file install -----
    def _backup_path(self, remote: str) -> str:
        return f"{BACKUPS}/{self.run_id}/original{remote}"

    def _unchanged(self, remote: str, source: Src, size_now: int) -> bool:
        known = (self.previous or {}).get("files", {}).get(remote)
        if known and remote.endswith(BRANDED) and known.get("sha256") == source.sha256(self.hasher):
            return True  # the names-and-icons step owns these after install; the package copy is unchanged
        if known and known.get("sha256") == source.sha256(self.hasher) and known.get("remote_size") == size_now:
            return True
        if is_self(remote) and not self.target.practice:
            return False  # the PS5 changes these on upload, so they cannot be compared by content
        if size_now == source.size and size_now <= READBACK_LIMIT:
            return self.target.sha256(remote) == source.sha256(self.hasher)
        return False

    def put(self, remote: str, source: Src, component: str, progress=None, create_only=False) -> str:
        """Install one file. Returns 'new', 'replaced', 'same' or 'kept'."""
        size_now = self.target.size(remote)
        if size_now is not None:
            if create_only:
                return "kept"
            if self._unchanged(remote, source, size_now):
                self._remember(remote, source, component, size_now)
                if progress:
                    progress(source.size)
                return "same"
        staged = remote + ".exbox-new"
        with source.open() as stream:
            self.target.upload(staged, stream, progress)
        staged_size = self.target.size(staged)
        exact = self.target.practice or not is_self(remote)
        if exact and staged_size != source.size:
            self.target.delete(staged)
            raise RuntimeError(f"{remote} did not arrive complete ({staged_size} of {source.size} bytes).")
        if not exact and not staged_size:
            self.target.delete(staged)
            raise RuntimeError(f"{remote} did not arrive.")
        if exact and source.size <= READBACK_LIMIT and self.target.sha256(staged) != source.sha256(self.hasher):
            self.target.delete(staged)
            raise RuntimeError(f"{remote} did not read back correctly; the old file was left in place.")
        if size_now is not None:
            backup = self._backup_path(remote)
            self.target.rename(remote, backup)
            self.journal.append(dict(path=remote, action="replaced", backup=backup))
            try:
                self.target.rename(staged, remote)
            except Exception:
                self.target.rename(backup, remote)
                self.journal.pop()
                raise
        else:
            self.journal.append(dict(path=remote, action="created"))
            self.target.rename(staged, remote)
        self._save_journal()
        self._remember(remote, source, component, self.target.size(remote))
        return "replaced" if size_now is not None else "new"

    def _remember(self, remote, source, component, remote_size):
        self.files[remote] = dict(component=component, size=source.size, sha256=source.sha256(self.hasher),
                                  remote_size=remote_size)

    def keep_copy(self, remote: str):
        """Back up a small file that a later step edits in place (autoload, names, icons, app database)."""
        data = self.target.read(remote)
        if data is None:
            self.journal.append(dict(path=remote, action="created"))
        else:
            backup = self._backup_path(remote)
            self.target.write(backup, data)
            self.journal.append(dict(path=remote, action="replaced", backup=backup))
            copy = self.work / "console-copies" / remote.lstrip("/")
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_bytes(data)
        self._save_journal()

    def _save_journal(self):
        self.work.mkdir(parents=True, exist_ok=True)
        text = json.dumps(dict(run=self.run_id, target=self.target.label, entries=self.journal), indent=1)
        (self.work / "manifest.json").write_text(text)

    def put_all(self, files: dict[str, Src], component: str, index, total, title) -> dict:
        counts = dict(new=0, replaced=0, same=0, kept=0)
        whole = sum(s.size for s in files.values()) or 1
        done = [0]

        def moved(count):
            done[0] += count
            self.tick(index, total, title, min(done[0] / whole, 1.0))
        for number, (remote, source) in enumerate(files.items(), 1):
            self.tick(index, total, title, min(done[0] / whole, 1.0), f"{number} of {len(files)}")
            before = done[0]
            counts[self.put(remote, source, component, moved)] += 1
            done[0] = before + source.size
        return counts

    # ----- the steps -----
    def plan(self) -> list[dict]:
        """What Install will do, in order, in plain English. Nothing is changed."""
        s, o = self.src, self.options
        by_id = {item["id"]: item for item in self.scan["items"]}
        on_console = self.target.size(TITLES["dashboard"] + "/eboot.bin") is not None
        steps = [dict(id="preflight", title="Check the PS5", do=True, why="")]

        def step(step_id, title, do, why=""):
            steps.append(dict(id=step_id, title=title, do=bool(do), why="" if do else why))

        step("dashboard", "Install the Ex-Box 360 dashboard", s["dashboard_package"],
             "Already on your PS5; kept as it is." if on_console else by_id["dashboard-app"]["missing"])
        art = s["prepared_art"] or (s["dashboard_inputs"] and o["prepare_art"])
        step("art", "Install your dashboard art", art, "No dashboard art in your folder; Ex-Box uses its plain look.")
        step("kit", "Install your avatar kit", s["kit"], by_id["avatar-kit"]["missing"])
        step("fonts", "Install your fonts", s["fonts"], by_id["fonts"]["missing"])
        step("trailers", "Install your trailers", s["trailers_packed"], by_id["trailers"]["detail"]
             or by_id["trailers"]["missing"])
        step("music", "Install your music", s["music"], by_id["music"]["missing"])
        step("editor", "Build and install the Avatar Editor" if s.get("editor_mode") == "build"
             else "Install the Avatar Editor", s.get("editor_mode"), by_id["editor"]["detail"]
             or by_id["editor"]["missing"])
        step("ps5x360", "Install PS5X360 (Xbox 360 games)", o["ps5x360"], "You chose not to install PS5X360.")
        step("companion", "Install the launch helper", s["companion"] and o["companion"],
             (by_id["companion"]["detail"] or by_id["companion"]["missing"]) if not s["companion"]
             else "You chose not to install it.")
        step("record", "Remember what was installed", True)
        step("branding", "Set the home-screen names and icons", True)
        return steps

    def run(self) -> dict:
        plan = self.plan()
        total = len(plan)
        started = time.time()
        result = dict(run=self.run_id, target=self.target.label, practice=self.target.practice, steps=[],
                      ok=False, restart_needed=False)
        handlers = dict(preflight=self._preflight, dashboard=self._dashboard, art=self._art, kit=self._kit,
                        fonts=self._fonts, trailers=self._trailers, music=self._music, editor=self._editor,
                        ps5x360=self._ps5x360, companion=self._companion, record=self._record,
                        branding=self._branding)
        failed = None
        try:
            for index, step in enumerate(plan, 1):
                entry = dict(id=step["id"], title=step["title"])
                if not step["do"]:
                    entry.update(status="skipped", text=step["why"])
                    self.log(f"Skipped: {step['title']}. {step['why']}")
                else:
                    self.tick(index, total, step["title"])
                    self.log(f"{step['title']}... ({index} of {total})")
                    try:
                        status, text = handlers[step["id"]](index, total, step["title"])
                    except Cancelled:
                        raise
                    except Exception as error:  # one plain line per failure; details go to the log file
                        status, text = "failed", str(error) or error.__class__.__name__
                        if step["id"] in ("preflight", "dashboard"):
                            failed = step["title"]
                    entry.update(status=status, text=text)
                    self.log(f"  {status}: {text}")
                    self.tick(index, total, step["title"], 1.0)
                result["steps"].append(entry)
                if failed:
                    break
        except Cancelled:
            failed = "Stopped by you"
            self.log("Stopped. Use Restore to put back anything that was replaced.")
        finally:
            self.hasher.save()
        problems = [s for s in result["steps"] if s["status"] == "failed"]
        result["ok"] = not failed and not problems
        result["restart_needed"] = self.restart_needed
        result["seconds"] = round(time.time() - started, 1)
        result["backups"] = sum(1 for e in self.journal if e["action"] == "replaced")
        result["summary"] = self._summary(result, failed, problems)
        self._write_log(result)
        result["log_file"] = str(self.work / "install-log.txt")
        return result

    def _summary(self, result, failed, problems) -> str:
        where = "the practice folder (no PS5 was contacted)" if self.target.practice else "your PS5"
        if failed:
            return f"Stopped at \"{failed}\". Nothing else was changed. Press Restore to undo this run."
        done = [s["title"] for s in result["steps"] if s["status"] in ("done", "simulated")]
        skipped = [s["title"] for s in result["steps"] if s["status"] == "skipped"]
        text = f"Done: {len(done)} step(s) finished on {where}"
        if skipped:
            text += f", {len(skipped)} optional step(s) skipped"
        if problems:
            text += f", {len(problems)} step(s) had a problem (see below)"
        text += "."
        if self.restart_needed and not self.target.practice:
            text += " Restart your PS5 to see the new names and icons, then open Ex-Box 360."
        elif not self.target.practice and not problems:
            text += " Open Ex-Box 360 on your PS5."
        return text

    def _write_log(self, result):
        self.work.mkdir(parents=True, exist_ok=True)
        lines = [f"Ex-Box Setup {VERSION}  run {self.run_id}  target: {self.target.label}", ""]
        lines += self.log_lines + ["", result["summary"]]
        (self.work / "install-log.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def _counts(self, counts: dict, what: str) -> str:
        bits = []
        if counts["new"]:
            bits.append(f"{counts['new']} new")
        if counts["replaced"]:
            bits.append(f"{counts['replaced']} replaced (old copies backed up)")
        if counts["same"]:
            bits.append(f"{counts['same']} already up to date")
        return f"{what}: " + (", ".join(bits) or "nothing to do") + "."

    def _preflight(self, index, total, title):
        if self.target.practice:
            return "done", "Practice run: files go to a folder on this PC. No PS5 is contacted."
        notes = []
        if not self.target.is_dir("/data/homebrew"):
            raise RuntimeError("The PS5 answered, but /data/homebrew was not found. Is the homebrew setup "
                               "(jailbreak and ShadowMountPlus) running? The installer does not install those.")
        notes.append("Payload Manager folder found." if self.target.is_dir("/data/pldmgr")
                     else "Payload Manager folder not found (the launch helper needs it).")
        notes.append("Free space cannot be read over FTP, so each file is checked as it arrives.")
        return "done", " ".join(notes)

    def _dashboard(self, index, total, title):
        files = tree(Path(self.src["dashboard_package"]), TITLES["dashboard"])
        counts = self.put_all(files, "dashboard-app", index, total, title)
        return "done", self._counts(counts, f"{len(files)} dashboard file(s)")

    def _art(self, index, total, title):
        private = self.src["prepared_art"]
        if not private:
            private = self._prepare_art(index, total, title)
        files = art_files(Path(private))
        if not files:
            return "skipped", "The art folder held nothing the dashboard uses."
        counts = self.put_all(files, "art", index, total, title)
        return "done", self._counts(counts, f"{len(files)} art file(s)")

    def _prepare_art(self, index, total, title) -> str:
        """Turn original dashboard files into ready art with the existing setup.py pipeline (local only)."""
        out = self.work / "prepared"
        inputs = Path(self.src["folder"])
        python = [sys.executable, str(DASHBOARD / "setup.py")]
        if not (inputs / "dashboard-resources").is_dir():
            inputs = self.work / "inputs"
            self._run(python + ["--inputs", str(inputs), "--intake", *self.src["dashboard_inputs"]],
                      "Sorting your dashboard files")
        self.tick(index, total, title, 0.1, "Preparing art from your files")
        self._run(python + ["--inputs", str(inputs), "--output", str(out), "--components", "dashboard"],
                  "Preparing dashboard art")
        return str(out / "private")

    def _run(self, command, what):
        result = subprocess.run(command, cwd=DASHBOARD, text=True, encoding="utf-8", errors="replace",
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (self.work / "prepare.log").parent.mkdir(parents=True, exist_ok=True)
        with (self.work / "prepare.log").open("a", encoding="utf-8") as log:
            log.write(f"$ {what}\n{result.stdout}\n")
        if result.returncode:
            last = [line for line in result.stdout.strip().splitlines() if line.strip()][-1:] or ["no details"]
            raise RuntimeError(f"{what} did not finish: {last[0][:300]}")

    def _kit(self, index, total, title):
        kit = Path(self.src["kit"])
        counts = self.put_all({f"{OLD_APP}/private_art/avatar/kit360.nxak": Src(kit)}, "avatar-kit",
                              index, total, title)
        return "done", self._counts(counts, "Avatar kit")

    def _fonts(self, index, total, title):
        files = {f"{OLD_APP}/private_art/fonts/{Path(p).name}": Src(Path(p)) for p in self.src["fonts"]}
        return "done", self._counts(self.put_all(files, "fonts", index, total, title), f"{len(files)} font(s)")

    def _trailers(self, index, total, title):
        files = trailer_files(Path(self.src["trailers_packed"]))
        return "done", self._counts(self.put_all(files, "trailers", index, total, title),
                                    f"{len(files)} trailer file(s)")

    def _music(self, index, total, title):
        root = Path(self.src["folder"])
        songs = [Path(p) for p in self.src["music"]]
        common = Path(os.path.commonpath([str(p.parent) for p in songs])) if songs else root
        files = {f"/data/nxe/music/{p.relative_to(common).as_posix()}": Src(p) for p in songs}
        return "done", self._counts(self.put_all(files, "music", index, total, title), f"{len(files)} song(s)")

    def _editor(self, index, total, title):
        package = self.src["editor_package"]
        if self.src.get("editor_mode") == "build":
            if not self.options["build_editor"]:
                return "simulated", ("Practice run: the Avatar Editor build was not started (it takes a long "
                                     "time). A real install builds it here from your three files.")
            package = editor.build(self.src["editor_originals"], Path(self.src["editor_dependencies"]),
                                   self.work / "editor-build", self.log,
                                   lambda fraction, detail: self.tick(index, total, title, fraction * 0.8, detail))
        files = editor.package_files(Path(package), TITLES["editor"], Src)
        counts = self.put_all(files, "editor", index, total, title)
        return "done", self._counts(counts, f"{len(files)} Avatar Editor file(s)")

    def _ps5x360(self, index, total, title):
        local = self.src["ps5x360_zip"]
        if not local:
            if not self.options["allow_download"]:
                return "simulated", (f"Practice run: PS5X360 {ps5x360.VERSION} was not downloaded. A real install "
                                     f"fetches it from the official release ({ps5x360.PAGE}) and checks it.")
            self.log(f"Downloading PS5X360 {ps5x360.VERSION} from its official release page")
            local = ps5x360.fetch(home() / "downloads",
                                  lambda done, size: self.tick(index, total, title, 0.5 * done / size,
                                                               "Downloading from the official release"))
        local = Path(local)
        ps5x360.check(local)
        files = {f"{TITLES['ps5x360']}/{name}": Src(zip_path=local, member=info)
                 for name, info in ps5x360.members(local)}
        if not any(remote.endswith("/LICENSE") for remote in files):
            raise RuntimeError("The PS5X360 file has no licence notice; it was not installed.")
        counts = self.put_all(files, "ps5x360", index, total, title)
        self.log(ps5x360.notice())
        return "done", self._counts(counts, f"PS5X360 {ps5x360.VERSION} by {ps5x360.AUTHOR} ({ps5x360.LICENCE})")

    def _companion(self, index, total, title):
        source = Path(self.src["companion"])
        name = "exbox360-daemon.elf"
        counts = self.put_all({f"{PAYLOAD_DIR}/{name}": Src(source)}, "companion", index, total, title)
        self.put("/data/nxe/companion.conf", Src(data=b"dashboard=PPSA99735\n"), "companion", create_only=True)
        current = self.target.read(AUTOLOAD, limit=1 << 20)
        fixed = fix_autoload(current, name)
        if fixed != current:
            self.keep_copy(AUTOLOAD)
            self.target.write(AUTOLOAD, fixed)
            if self.target.read(AUTOLOAD, limit=1 << 20) != fixed:
                raise RuntimeError("The autoload list did not save correctly. Press Restore to put it back.")
            note = " Start-up entry written" + (" (the old list was backed up)." if current is not None else ".")
        else:
            note = " Start-up entry was already correct."
        return "done", self._counts(counts, "Launch helper") + note + " It starts the next time the PS5 boots."

    def _record(self, index, total, title):
        files = dict((self.previous or {}).get("files", {}))
        files.update(self.files)
        components: dict[str, dict] = {}
        for row in files.values():
            entry = components.setdefault(row["component"], dict(files=0, bytes=0))
            entry["files"] += 1
            entry["bytes"] += row["size"]
        if "ps5x360" in components:
            components["ps5x360"].update(version=ps5x360.VERSION, author=ps5x360.AUTHOR, licence=ps5x360.LICENCE)
        record = dict(schema=1, installer=VERSION, run=self.run_id,
                      installed=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                      components=components,
                      unlocked=[i["id"] for i in self.scan["items"] if i["status"] == "found"],
                      folder=catalog.folder_fingerprint(self.scan), files=files)
        data = (json.dumps(record, indent=1, sort_keys=True) + "\n").encode()
        if self.target.size(RECORD) is not None:
            self.keep_copy(RECORD)
        else:
            self.journal.append(dict(path=RECORD, action="created"))
            self._save_journal()
        self.target.write(RECORD, data)
        self.target.write(f"{BACKUPS}/{self.run_id}/manifest.json",
                          json.dumps(dict(run=self.run_id, entries=self.journal), indent=1).encode())
        local = pc_record_path(self.target)
        local.parent.mkdir(parents=True, exist_ok=True)
        local.write_bytes(data)
        return "done", f"Install record saved ({len(files)} file(s) remembered; names and fingerprints only)."

    def _branding(self, index, total, title):
        sys.path.insert(0, str(DASHBOARD / "tools"))
        import branding
        style = self.options["icons"]
        loading = Path(self.src["loading"]) if self.src["loading"] else None
        avatar = Path(self.src["avatar_render"]) if self.src["avatar_render"] else None
        note = ""
        if style == "xbox" and not loading:
            style, note = "default", " Your loading picture was not found, so the Ex-Box default icons were used."
        if style == "xbox":
            try:
                from PIL import Image  # needed only to draw the Xbox-style icons
                with Image.open(loading) as picture:
                    picture.verify()
            except ImportError:
                style, note = "default", (" Xbox-style icons need the Pillow imaging add-on, which this PC's "
                                          "Python does not have, so the Ex-Box default icons were used.")
            except Exception:
                style, note = "default", (" Your loading picture could not be opened, so the Ex-Box default "
                                          "icons were used.")
        for tid in branding.TITLES:
            for folder in (f"/data/homebrew/{tid}/sce_sys", f"/user/appmeta/{tid}"):
                for name in ("param.json", "icon0.png"):
                    if self.target.size(f"{folder}/param.json") is not None:
                        self.keep_copy(f"{folder}/{name}")
        if self.target.size(branding.APP_DB) is not None:
            self.keep_copy(branding.APP_DB)
        result = branding.apply_branding(getattr(self.target, "host", "practice"), style=style, loading=loading,
                                         avatar=avatar, backup_dir=self.work / "console-copies",
                                         log=self.log, connection=FtpLike(self.target))
        self.restart_needed = bool(result.get("restart_needed"))
        if not result["titles"]:
            return "skipped", "Neither Ex-Box title is installed yet, so there was nothing to name."
        names = " and ".join(result["titles"].values())
        icons = "Xbox-style icons from your files" if style == "xbox" else "Ex-Box default icons"
        return "done", f"Named {names}; {icons}.{note}"


def fix_autoload(current: bytes | None, name: str) -> bytes:
    """Make sure the autoload list has our helper as a bare file name, exactly once.

    Payload Manager silently skips absolute paths, so an old absolute entry for our own helper is
    replaced. Every other line, and the file's line endings, stay exactly as they were.
    """
    text = (current or b"").decode("utf-8", errors="surrogateescape")
    newline = "\r\n" if "\r\n" in text else "\n"
    ours = {"exbox360-daemon.elf", "nxe_dashboard_daemon.elf"}
    lines = text.split(newline) if text else []
    kept, present = [], False
    for line in lines:
        stripped = line.strip()
        if stripped.rsplit("/", 1)[-1] in ours and (stripped.startswith("/") or stripped in ours):
            if stripped == name and not present:
                kept.append(line)
                present = True
            continue
        kept.append(line)
    if not present:
        while kept and kept[-1] == "":
            kept.pop()
        kept.append(name)
        kept.append("")
    return newline.join(kept).encode("utf-8", errors="surrogateescape")


# ---------- restore and doctor ----------
def last_manifest(target: Target) -> dict | None:
    runs = home() / "runs"
    if not runs.is_dir():
        return None
    for folder in sorted((p for p in runs.iterdir() if p.is_dir()), reverse=True):
        path = folder / "manifest.json"
        if path.is_file():
            data = json.loads(path.read_text())
            if data.get("target") == target.label and data.get("entries"):
                return data
    return None


def restore(target: Target, log=print) -> dict:
    """Undo the most recent run on this target: put back what was replaced, remove what was added."""
    manifest = last_manifest(target)
    if not manifest:
        return dict(ok=False, summary="There is nothing to restore for this PS5 yet.")
    put_back = removed = problems = 0
    for entry in reversed(manifest["entries"]):
        try:
            if entry["action"] == "created":
                if target.size(entry["path"]) is not None:
                    target.delete(entry["path"])
                    removed += 1
            elif target.size(entry["backup"]) is not None:
                target.delete(entry["path"])
                target.rename(entry["backup"], entry["path"])
                put_back += 1
        except Exception as error:
            problems += 1
            log(f"Could not restore {entry['path']}: {error}")
    path = home() / "runs" / manifest["run"] / "manifest.json"
    path.rename(path.with_name("manifest.restored.json"))
    summary = f"Restored: {put_back} file(s) put back, {removed} added file(s) removed."
    if problems:
        summary += f" {problems} file(s) could not be restored; see the log."
    return dict(ok=not problems, summary=summary, run=manifest["run"])


def doctor(target: Target) -> dict:
    """Read-only health check against the install record."""
    record = load_record(target)
    if not record:
        return dict(ok=False, lines=["No install record was found. Ex-Box has not been installed with this "
                                     "installer yet."], wrong=[])
    wrong, by_component = [], {}
    for remote, row in record["files"].items():
        if remote.endswith(BRANDED):
            continue  # the names-and-icons step rewrites these on purpose
        size = target.size(remote)
        state = "ok" if size == row["remote_size"] else ("missing" if size is None else "changed")
        counts = by_component.setdefault(row["component"], dict(ok=0, missing=0, changed=0))
        counts[state] += 1
        if state != "ok":
            wrong.append(remote)
    titles = {item["id"]: item["title"] for item in catalog.checklist_template()}
    titles["art"] = "Dashboard art"
    lines = []
    for component, counts in by_component.items():
        name = titles.get(component, component)
        if counts["missing"] or counts["changed"]:
            lines.append(f"{name}: {counts['missing']} file(s) missing, {counts['changed']} changed. "
                         "Press Install to repair.")
        else:
            lines.append(f"{name}: all {counts['ok']} file(s) present.")
    if "companion" in by_component:
        autoload = target.read(AUTOLOAD, limit=1 << 20) or b""
        good = b"exbox360-daemon.elf" in [line.strip() for line in autoload.splitlines()]
        lines.append("Launch helper start-up entry: " + ("correct." if good else "missing or wrong. Press "
                                                         "Install to repair."))
        if not good:
            wrong.append(AUTOLOAD)
    return dict(ok=not wrong, lines=lines, wrong=wrong, installed=record.get("installed"))


def practice_target() -> LocalTarget:
    return LocalTarget(home() / "practice-console")
