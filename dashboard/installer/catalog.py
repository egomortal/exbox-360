"""The one-folder scan: what the installer looks for, whether it was found, and what it unlocks.

scan(folder) reads the folder only. It never copies, changes or uploads anything.
Every checklist line carries plain-English text for both outcomes:
  unlocks  - what you get when the file is there
  missing  - what you lose without it
  source   - where that kind of file comes from (no links to copyrighted material)
"""
from __future__ import annotations

import hashlib
import json
import os
import stat
import zipfile
from pathlib import Path

from . import ps5x360

HERE = Path(__file__).resolve().parent
DASHBOARD = HERE.parent
REPO = DASHBOARD.parent
REGISTRY = DASHBOARD / "tools" / "population"
BUILDER = REPO / "avatar-editor" / "local-builder"

MAX_FILES = 200_000
MAX_DEPTH = 12

# Dashboard looks, by the system-software revision their art comes from.
ERAS = [
    ("era-blades", "Blades (2005)", 1888),
    ("era-blades2", "Blades, later version", 6770),
    ("era-nxe", "NXE (2008)", 9199),
    ("era-kinect", "Kinect (2010)", 13604),
    ("era-metro", "Metro (2011)", 15574),
    ("era-metro2", "Metro, later version", 17559),
]
DASH_SOURCE = ("Your own Xbox 360's system files: a system-update download for that dashboard version, "
               "or a copy of the dashboard files from your console's storage.")

VIDEO = {".mp4", ".m4v", ".mov", ".mkv", ".webm", ".wmv"}
AUDIO = {".mp3", ".flac", ".ogg", ".oga", ".wav"}
FONTS = {".ttf", ".otf", ".xtt"}
COMPANION_NAMES = {"exbox360-daemon.elf", "nxe_dashboard_daemon.elf"}
# Never descend into these: build output and tool folders hold thousands of unrelated files.
SKIP_DIRS = {".git", "__pycache__", "node_modules", "native-work", "build", "$recycle.bin",
             "system volume information"}


def _pins() -> dict:
    """Pinned fingerprints of the editor inputs, read from the editor builder's own contracts."""
    pins = {"editor_xex": None, "font_xtt": None, "splash": None}
    try:
        pins["editor_xex"] = json.loads((BUILDER / "guest-contract.json").read_text())["xex_sha256"]
    except (OSError, KeyError, ValueError):
        pass
    try:
        text = (BUILDER / "companion-inputs.json").read_text()
        data = json.loads(text)
        for value in _walk(data):
            if isinstance(value, dict) and "produced_ttf_sha256" in value:
                pins["font_xtt"] = value.get("sha256")
    except (OSError, ValueError):
        pass
    try:
        for value in _walk(json.loads((BUILDER / "splash-contract.json").read_text())):
            if isinstance(value, dict):
                for key, item in value.items():
                    if "sha256" in key and isinstance(item, str) and item.startswith("2881b4e9"):
                        pins["splash"] = item
    except (OSError, ValueError):
        pass
    pins["splash"] = pins["splash"] or "2881b4e94e386d6e15b03dc4e229ac006e3897e36d38b21c14a151198c0081fb"
    return pins


def _walk(value):
    yield value
    if isinstance(value, dict):
        for item in value.values():
            yield from _walk(item)
    elif isinstance(value, list):
        for item in value:
            yield from _walk(item)


def _registry():
    """Known dashboard files: size -> rows, and known update archives by fingerprint."""
    by_size: dict[int, list] = {}
    updates: dict[str, dict] = {}
    try:
        for row in json.loads((REGISTRY / "known-modules.json").read_text())["modules"]:
            by_size.setdefault(int(row.get("bytes", -1)), []).append(row)
        for row in json.loads((REGISTRY / "known-updates.json").read_text())["archives"]:
            updates[row["archive_sha256"]] = row
    except (OSError, KeyError, ValueError):
        pass
    return by_size, updates


def _era_of(path_text: str, build: int | None = None):
    parts = path_text.replace("\\", "/").split("/")
    for item_id, _title, revision in ERAS:
        if build == revision or any(p == str(revision) or p.startswith(f"{revision} ") for p in parts):
            return item_id
    return None


class Hasher:
    """SHA-256 with a small cache keyed by path, size and time, so a re-scan of a big folder is quick."""

    def __init__(self, cache_file: Path | None = None):
        self.cache_file = cache_file
        self.cache: dict = {}
        if cache_file and cache_file.is_file():
            try:
                self.cache = json.loads(cache_file.read_text())
            except (OSError, ValueError):
                self.cache = {}
        self.dirty = False

    def sha256(self, path: Path) -> str:
        stat = path.stat()
        key = f"{path}|{stat.st_size}|{stat.st_mtime_ns}"
        if key in self.cache:
            return self.cache[key]
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1 << 20), b""):
                digest.update(block)
        self.cache[key] = digest.hexdigest()
        self.dirty = True
        return self.cache[key]

    def save(self):
        if self.cache_file and self.dirty:
            try:
                self.cache_file.parent.mkdir(parents=True, exist_ok=True)
                self.cache_file.write_text(json.dumps(self.cache))
            except OSError:
                pass


def _head(path: Path, count: int = 16) -> bytes:
    try:
        with path.open("rb") as stream:
            return stream.read(count)
    except OSError:
        return b""


def _plain(path: Path, kind) -> bool:
    try:
        return not path.is_symlink() and kind(path.stat().st_mode)
    except OSError:
        return False


def _files(root: Path):
    count = 0
    for base, dirs, names in os.walk(root, followlinks=False):
        depth = len(Path(base).relative_to(root).parts)
        dirs[:] = sorted(d for d in dirs if d.lower() not in SKIP_DIRS
                         and _plain(Path(base) / d, stat.S_ISDIR)) if depth < MAX_DEPTH else []
        for name in sorted(names):
            path = Path(base) / name
            if not _plain(path, stat.S_ISREG):
                continue  # links, devices and files Windows cannot open are not inputs
            count += 1
            if count > MAX_FILES:
                raise ValueError(f"That folder holds more than {MAX_FILES:,} files. Pick the folder that "
                                 "holds just your Xbox 360 files.")
            yield path


def _size_text(count: int) -> str:
    for unit in ("bytes", "KB", "MB", "GB"):
        if count < 1024 or unit == "GB":
            return f"{count:,.0f} {unit}" if unit in ("bytes", "KB") else f"{count:,.1f} {unit}"
        count /= 1024
    return str(count)


def _item(item_id, group, title, unlocks, missing, source, required=False):
    return dict(id=item_id, group=group, title=title, status="missing", required=required,
                detail="", unlocks=unlocks, missing=missing, source=source, files=[], notes=[])


def checklist_template() -> list[dict]:
    """Every line the installer shows, in display order, all marked missing."""
    items = [
        _item("dashboard-app", "Ex-Box", "Ex-Box 360 dashboard app",
              "The dashboard itself. It installs as \"Ex-Box 360\" on your PS5 home screen.",
              "There is nothing to install, unless Ex-Box is already on your PS5.",
              "It comes inside the Ex-Box Setup download (it is our own code). If it is missing, "
              "download Ex-Box Setup again.", required=True),
    ]
    for item_id, title, revision in ERAS:
        items.append(_item(item_id, "Dashboard looks", f"{title} dashboard art",
                           f"The real {title} look: its backgrounds, tiles and panels.",
                           f"The {title} look uses Ex-Box's plain built-in art instead of the real thing.",
                           DASH_SOURCE))
    items += [
        _item("shared-art", "Dashboard looks", "Shared artwork (buttons, guide, icons)",
              "The real A/B/X/Y button pictures, guide artwork and small icons in every look.",
              "Buttons and guide panels use Ex-Box's own plain drawings.",
              "The shared-resources file (shrdres.xzp) from your own Xbox 360's system files."),
        _item("fonts", "Dashboard looks", "Xbox 360 fonts",
              "The dashboard's real typeface in menus and titles.",
              "Text uses the free Inter font. Everything still works.",
              "The font files from your own Xbox 360's system files (for example xenonclatin.xtt), "
              "or fonts you already own."),
        _item("avatar-kit", "Avatars", "Avatar kit",
              "Live 3D avatars on the dashboard and in Theater Party.",
              "The dashboard shows a simple stand-in figure instead of your avatar.",
              "Built on your PC from the avatar files on your own Xbox 360 (the kit file ends in .nxak)."),
        _item("editor", "Avatars", "Avatar Editor",
              "The Avatar Editor, installed as \"Ex-Box Avatars\": change clothes, features and props.",
              "No Avatar Editor. The dashboard and your current avatar still work.",
              "Built on your PC from your own copy of the Xbox 360 Avatar Editor (avatareditor.xex), "
              "its font (xenonclatin.xtt) and the Xbox 360 start-up picture (splash_360.png), plus the "
              "build tools listed in the editor builder's README."),
        _item("trailers", "Media", "Trailers",
              "Video trailers that play on the dashboard.",
              "The trailer spots stay empty.",
              "Your own video files, or trailers already prepared by Ex-Box's trailer tool."),
        _item("music", "Media", "Music",
              "Your songs in the dashboard's Music Library.",
              "The Music Library is empty until you add songs.",
              "Your own music files (mp3, flac, ogg or wav)."),
        _item("ps5x360", "Xbox 360 games", "PS5X360 (Xbox 360 games) by BrinooTk",
              "Play Xbox 360 games, started from the Ex-Box Games tab. PS5X360 is a separate free "
              "program by BrinooTk (MIT licence). It is fetched from its official GitHub release and checked.",
              "Xbox 360 games cannot be started. Everything else works.",
              "Downloaded by this installer from the official PS5X360 release page, or put the release "
              "file PPSA50011.zip in your folder to skip the download."),
        _item("games360", "Xbox 360 games", "Your Xbox 360 games",
              "These games show in the Games tab and start through PS5X360.",
              "The Games tab shows only your PS5 games.",
              "Copies of games you own. The installer never downloads games, BIOS files or keys."),
        _item("icons-art", "Finishing touches", "Loading art for Xbox-style icons",
              "Xbox-style home-screen icons made from your own loading picture.",
              "The home-screen icons use the Ex-Box default artwork.",
              "The Xbox 360 start-up picture (splash_360.png) from your own console's system files."),
        _item("companion", "Finishing touches", "Launch helper",
              "Open the Avatar Editor and Xbox 360 games from inside Ex-Box, and come back afterwards.",
              "Open the Avatar Editor and PS5X360 from the PS5 home screen instead. Nothing else is lost.",
              "Built on your own PC from its source. Ex-Box Setup does not include a ready-made copy; "
              "see docs/INSTALLER.md."),
    ]
    return items


def scan(folder, hasher: Hasher | None = None, package_dirs: list[Path] | None = None) -> dict:
    """Look through one folder and return the checklist. Read-only."""
    root = Path(folder).expanduser()
    if not root.is_dir():
        raise ValueError("That folder was not found. Pick the folder that holds your Xbox 360 files.")
    root = root.resolve()
    hasher = hasher or Hasher()
    pins = _pins()
    by_size, updates = _registry()
    items = {item["id"]: item for item in checklist_template()}
    found: dict[str, list] = {key: [] for key in items}
    sources: dict = dict(folder=str(root), editor_originals={}, editor_dependencies=None, editor_package=None,
                         dashboard_package=None, prepared_art=None, kit=None, fonts=[], trailers_packed=None,
                         trailer_videos=[], music=[], music_root=None, loading=None, avatar_render=None,
                         ps5x360_zip=None, companion=None, shared=None, games=[], dashboard_inputs=[])
    wrong: list[str] = []
    total = 0

    def note(item_id, path, sha=None):
        relative = path.relative_to(root).as_posix()
        found[item_id].append(dict(name=relative, size=path.stat().st_size, sha256=sha))

    god_dirs = set()
    for path in _files(root):
        total += 1
        name = path.name.lower()
        ext = path.suffix.lower()
        size = path.stat().st_size
        relative = path.relative_to(root).as_posix()
        parts = [p.lower() for p in path.relative_to(root).parts]

        # --- packages (a ready dashboard or editor folder, by title ID) ---
        if name == "eboot.bin" and len(parts) >= 2 and parts[-2] in ("ppsa99735", "ppsa99734"):
            which = "dashboard_package" if parts[-2] == "ppsa99735" else "editor_package"
            if sources[which] is None:
                sources[which] = str(path.parent)
            note("dashboard-app" if parts[-2] == "ppsa99735" else "editor", path)
            continue
        if any(p in ("ppsa99735", "ppsa99734", "ppsa50011") for p in parts[:-1]):
            continue  # the rest of a package folder is installed with it, not scanned as inputs

        # --- editor inputs ---
        if name == "avatareditor.xex":
            sha = hasher.sha256(path)
            if pins["editor_xex"] and sha != pins["editor_xex"]:
                wrong.append(f"{relative}: this is a different version of the Avatar Editor than the one "
                             "Ex-Box supports (it needs the 2.0.12581 editor). It comes from your own "
                             "Xbox 360's system files.")
            else:
                sources["editor_originals"]["avatareditor.xex"] = str(path)
                note("editor", path, sha)
            continue
        if name == "xenonclatin.xtt":
            sha = hasher.sha256(path)
            note("fonts", path, sha)
            sources["fonts"].append(str(path))
            if pins["font_xtt"] and sha != pins["font_xtt"]:
                items["editor"]["notes"].append("xenonclatin.xtt is a different version than the editor "
                                                "build supports; it is still used as a dashboard font.")
            else:
                sources["editor_originals"]["xenonclatin.xtt"] = str(path)
            continue
        if name == "splash_360.png":
            sha = hasher.sha256(path)
            if sha == pins["splash"]:
                sources["editor_originals"]["splash_360.png"] = str(path)
                sources["loading"] = sources["loading"] or str(path)
                note("icons-art", path, sha)
            else:
                wrong.append(f"{relative}: this start-up picture has been changed or is a different version. "
                             "The original is 671 x 416 pixels and comes from the 2.0.9199 system files.")
            continue
        if name in ("loading.png", "xbox-loading.png") and _head(path, 8) == b"\x89PNG\r\n\x1a\n":
            sources["loading"] = str(path)
            note("icons-art", path, hasher.sha256(path))
            continue
        if name in ("avatar.png", "avatar_body.png", "avatar-render.png") and sources["avatar_render"] is None:
            sources["avatar_render"] = str(path)
        if name.startswith("editor-") and ext == ".zip" and "builder" not in name:
            try:
                with zipfile.ZipFile(path) as archive:
                    if any(n.split("/")[-1] == "eboot.bin" for n in archive.namelist()):
                        sources["editor_package"] = str(path)
                        note("editor", path, hasher.sha256(path))
                        continue
            except (zipfile.BadZipFile, OSError):
                wrong.append(f"{relative}: this ZIP file is damaged.")
                continue

        # --- PS5X360 release file, companion, kit ---
        if name == ps5x360.ASSET.lower():
            sha = hasher.sha256(path)
            if sha == ps5x360.SHA256:
                sources["ps5x360_zip"] = str(path)
                note("ps5x360", path, sha)
            else:
                wrong.append(f"{relative}: this is not PS5X360 {ps5x360.VERSION} from the official release "
                             f"(or the download is damaged). The installer will fetch the right one itself.")
            continue
        if name in COMPANION_NAMES and _head(path, 4) == b"\x7fELF":
            sources["companion"] = str(path)
            note("companion", path, hasher.sha256(path))
            continue
        if ext == ".nxak":
            if _head(path, 4) == b"NXAK":
                if sources["kit"] is None or "360" in name:
                    sources["kit"] = str(path)
                note("avatar-kit", path, hasher.sha256(path))
            else:
                wrong.append(f"{relative}: this does not look like an Ex-Box avatar kit.")
            continue

        # --- dashboard art ---
        if name == "shrdres.xzp" and _head(path, 4) == b"XUIZ":
            sources["shared"] = str(path)
            sources["dashboard_inputs"].append(str(path))
            note("shared-art", path, hasher.sha256(path))
            continue
        if "dash.xex" in parts[:-1]:  # a prepared dashboard-resources tree: <era>/<revision>/dash.xex/<module>
            era = _era_of(relative)
            if era and _head(path, 4) == b"XUIZ":
                note(era, path)
                top = path
                while top.parent != root and top.parent.name.lower() != "dashboard-resources":
                    top = top.parent
                resource_root = top.parent if top.parent.name.lower() == "dashboard-resources" else None
                if resource_root and str(resource_root) not in sources["dashboard_inputs"]:
                    sources["dashboard_inputs"].append(str(resource_root))
            continue
        if size in by_size and ext in ("", ".xex", ".xzp", ".xexp"):
            sha = hasher.sha256(path)
            for row in by_size[size]:
                if row["sha256"] == sha:
                    era = _era_of(row["path"], row.get("declared_build"))
                    if era:
                        note(era, path, sha)
                        sources["dashboard_inputs"].append(str(path))
                    break
            else:
                if ext == ".xex" and "dash" in name:
                    wrong.append(f"{relative}: this dashboard file is not a version Ex-Box knows.")
            continue
        if ext == ".zip" and size > 20_000_000:
            sha = hasher.sha256(path)
            if sha in updates:
                era = _era_of("", updates[sha]["build"])
                if era:
                    note(era, path, sha)
                    sources["dashboard_inputs"].append(str(path))
                continue

        # --- prepared art, fonts, media ---
        if name == "background.png" and len(parts) >= 2 and parts[-2] == "ps5":
            sources["prepared_art"] = str(path.parent.parent)  # an Ex-Box "private" tree prepared earlier
        if ext in FONTS and name != "xenonclatin.xtt" and ("font" in parts[-2:-1] or "fonts" in parts[:-1]):
            sources["fonts"].append(str(path))
            note("fonts", path)
            continue
        if name == "manifest.tsv" and (path.parent / "packed").is_dir():
            sources["trailers_packed"] = str(path.parent)
            packed = [p for p in (path.parent / "packed").iterdir() if p.suffix.lower() == ".frames"]
            for p in packed:
                note("trailers", p)
            continue
        if ext in VIDEO and "packed" not in parts:
            head = _head(path, 12)
            if head[4:8] == b"ftyp" or head[:4] in (bytes.fromhex("1a45dfa3"), bytes.fromhex("3026b275")):
                sources["trailer_videos"].append(str(path))
            continue
        if ext in AUDIO and not any(p in ("sounds", "packed", "trailers") for p in parts[:-1]):
            sources["music"].append(str(path))
            note("music", path)
            continue

        # --- Xbox 360 games (counted only; never copied or changed) ---
        if ext == ".iso" and size > 100_000_000:
            sources["games"].append(relative)
        elif name == "default.xex":
            sources["games"].append(path.parent.relative_to(root).as_posix() or path.name)
        elif ext == ".god" or (ext == "" and size > 1024 and _head(path, 4) in (b"LIVE", b"PIRS", b"CON ")
                               and size > 50_000_000):
            god_dirs.add(relative)
    sources["games"].extend(sorted(god_dirs))

    # editor build tools: a "dependencies" folder laid out as the builder's README describes
    for candidate in [root / "dependencies", root / "editor-dependencies"] + \
            [p / "dependencies" for p in root.iterdir() if p.is_dir()]:
        if (candidate / "native-sdk").is_dir() and (candidate / "sources").is_dir():
            sources["editor_dependencies"] = str(candidate)
            break
    for extra in package_dirs or []:
        extra = Path(extra)
        if (extra / "PPSA99735" / "eboot.bin").is_file() and not sources["dashboard_package"]:
            sources["dashboard_package"] = str(extra / "PPSA99735")
            items["dashboard-app"]["notes"].append("Included with Ex-Box Setup.")

    if sources["prepared_art"]:  # art already prepared by Ex-Box's own tools counts for its look
        private = Path(sources["prepared_art"])
        ready = [("era-nxe", private / "xbox" / "unpacked")]
        ready += [(item_id, private / "era-library" / str(revision)) for item_id, _t, revision in ERAS[1:]
                  if revision != 9199]
        ready.append(("shared-art", private / "xbox" / "shrdres"))
        for item_id, folder in ready:
            if not found[item_id] and folder.is_dir() and any(folder.iterdir()):
                found[item_id].append(dict(name=folder.relative_to(root).as_posix() + "/", size=0, sha256=None))
                items[item_id]["notes"].append("Already prepared by Ex-Box's tools.")

    _fill(items, found, sources, pins)
    hasher.save()
    ordered = [items[item["id"]] for item in checklist_template()]
    return dict(folder=str(root), files_seen=total, items=ordered, wrong=wrong, sources=sources,
                preview=preview(ordered, sources))


def _fill(items, found, sources, pins):
    def done(item_id, detail, status="found"):
        items[item_id]["status"] = status
        items[item_id]["detail"] = detail
        items[item_id]["files"] = found[item_id][:200]

    if sources["dashboard_package"]:
        done("dashboard-app", "Ready to install.")
    for item_id, title, _revision in ERAS:
        if found[item_id]:
            ready = found[item_id][0]["name"].endswith("/")
            done(item_id, f"Art for the {title} look is ready." if ready
                 else f"{len(found[item_id])} file(s) for the {title} look.")
    if found["shared-art"]:
        done("shared-art", "Found shrdres.xzp." if sources["shared"] else "Found prepared shared artwork.")
    if found["fonts"]:
        done("fonts", f"{len(found['fonts'])} font file(s).")
    if sources["kit"]:
        size = sum(f["size"] for f in found["avatar-kit"] if f["name"].endswith(Path(sources["kit"]).name))
        done("avatar-kit", f"Found {Path(sources['kit']).name} ({_size_text(size)}).")
    originals = sources["editor_originals"]
    needed = ["avatareditor.xex", "xenonclatin.xtt", "splash_360.png"]
    if sources["editor_package"]:
        done("editor", "An Avatar Editor you already built was found. It installs as it is.")
        sources["editor_mode"] = "package"
    elif all(name in originals for name in needed) and sources["editor_dependencies"]:
        done("editor", "Your three editor files and the build tools were found. The editor is built on "
                       "this PC during Install (this can take a while).")
        sources["editor_mode"] = "build"
    elif originals:
        lacking = [name for name in needed if name not in originals]
        text = []
        if lacking:
            text.append("still needed: " + ", ".join(lacking))
        if not sources["editor_dependencies"]:
            text.append("the build tools folder (\"dependencies\") was not found")
        done("editor", "Some editor files were found, but " + "; ".join(text) + ". The editor is skipped.",
             status="partial")
        sources["editor_mode"] = None
    else:
        sources["editor_mode"] = None
    if sources["loading"]:
        extra = " Your avatar picture was found too." if sources["avatar_render"] else ""
        done("icons-art", "Found your loading picture." + extra)
    if sources["trailers_packed"]:
        done("trailers", f"{len(found['trailers'])} prepared trailer(s).")
    elif sources["trailer_videos"]:
        done("trailers", f"{len(sources['trailer_videos'])} video file(s) found, but they are not prepared "
                         "yet. Preparing videos inside the installer is not built yet; run "
                         "import_trailers.py first.", status="partial")
    if found["music"]:
        total = sum(f["size"] for f in found["music"])
        done("music", f"{len(found['music'])} song(s), {_size_text(total)}.")
    if sources["ps5x360_zip"]:
        done("ps5x360", f"PS5X360 {ps5x360.VERSION} found in your folder and checked. No download needed.")
    else:
        done("ps5x360", f"Will be downloaded from the official release ({ps5x360.VERSION}, "
                        f"{_size_text(ps5x360.SIZE)}) and checked. You can untick this.", status="download")
    if sources["games"]:
        done("games360", f"{len(sources['games'])} game(s) found. The installer does not copy games; "
                         "put them where PS5X360's own instructions say.")
    if sources["companion"]:
        done("companion", "Found the launch helper you built.")
    for item in items.values():
        if item["status"] == "missing":
            item["files"] = []


def preview(items: list[dict], sources: dict) -> dict:
    """The "what you'll get" summary shown before Install."""
    by_id = {item["id"]: item for item in items}
    get, skip = [], []
    eras = [title for item_id, title, _ in ERAS if by_id[item_id]["status"] == "found"]
    if by_id["dashboard-app"]["status"] == "found":
        get.append("Ex-Box 360 dashboard on your PS5 home screen.")
    if eras:
        get.append("Real dashboard looks: " + ", ".join(eras) + ".")
    missing_eras = [title for item_id, title, _ in ERAS if by_id[item_id]["status"] != "found"]
    if missing_eras:
        skip.append("Plain built-in art for: " + ", ".join(missing_eras) + ".")
    for item_id in ("shared-art", "fonts", "avatar-kit", "editor", "trailers", "music", "ps5x360",
                    "games360", "companion", "icons-art"):
        item = by_id[item_id]
        if item["status"] in ("found", "download"):
            get.append(item["unlocks"] if item_id != "games360" else item["detail"])
        else:
            skip.append(f"{item['title']}: {item['missing']}")
    return dict(get=get, skip=skip, eras=eras, games=len(sources["games"]),
                can_install=by_id["dashboard-app"]["status"] == "found")


def folder_fingerprint(result: dict) -> dict:
    """File names, sizes and fingerprints only. This is what the install record remembers."""
    rows = {}
    for item in result["items"]:
        for row in item["files"]:
            rows[row["name"]] = dict(item=item["id"], size=row["size"], sha256=row.get("sha256"))
    return rows


def new_since(result: dict, record: dict | None) -> list[str]:
    """Plain-English "new since last time" lines, comparing this scan with the last install record."""
    if not record:
        return []
    before = record.get("folder", {})
    now = folder_fingerprint(result)
    titles = {item["id"]: item["title"] for item in result["items"]}
    added: dict[str, int] = {}
    changed: dict[str, int] = {}
    for name, row in now.items():
        old = before.get(name)
        if old is None:
            added[row["item"]] = added.get(row["item"], 0) + 1
        elif old.get("size") != row["size"] or (old.get("sha256") and row.get("sha256")
                                               and old["sha256"] != row["sha256"]):
            changed[row["item"]] = changed.get(row["item"], 0) + 1
    lines = [f"{count} new file(s) for {titles.get(item, item)}" for item, count in added.items()]
    lines += [f"{count} changed file(s) for {titles.get(item, item)}" for item, count in changed.items()]
    was = set(record.get("unlocked", []))
    for item in result["items"]:
        if item["status"] == "found" and item["id"] not in was and item["id"] not in added:
            lines.append(f"New: {item['title']}")
    return lines
