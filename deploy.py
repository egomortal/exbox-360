"""Stage the native Ex-Box 360 ELF without touching other homebrew folders."""
from ftplib import FTP
from pathlib import Path, PurePosixPath
import sys

import argparse
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("host", nargs="?", default="192.168.1.189")
parser.add_argument("--no-elf", action="store_true", help="upload art, sounds and trailers only")
parser.add_argument("--resume", action="store_true", help="skip files already on the console at the same size")
parser.add_argument("--budget", type=float, default=0, help="stop starting new uploads after this many seconds")
parser.add_argument("--music", action="store_true", help="also send private/music/ to /data/nxe/music (the Music Library)")
parser.add_argument("--avatar-editor", action="store_true", help="also send verified private/avatar-editor assets; never sends saves or host binaries")
args = parser.parse_args()
host = args.host
started = time.monotonic()
root = Path(__file__).resolve().parent
files = {
    "eboot.next": root / "build" / "eboot.elf",
    "Inter-Regular.ttf": root.parent / "native-app" / "third_party" / "fonts" / "Inter-Regular.ttf",
    "Inter-SemiBold.ttf": root.parent / "native-app" / "third_party" / "fonts" / "Inter-SemiBold.ttf",
    "sce_sys/icon0.png": root.parent / "native-app" / "sce_sys" / "icon0.png",
}
files.update({f"kenney/{path.name}": path for path in (root / "assets" / "kenney").glob("*.png")})
files.update({f"shiny/{path.name}": path for path in (root / "assets" / "shiny").glob("*.png")})
files.update({f"cards/{path.name}": path for path in (root / "assets" / "cards").glob("*.png")})
private_buttons = root / "private" / "xbox" / "shrdres"
for name in ("A-Button.png", "B-Button.png", "X-Button.png", "Y-Button.png"):
    path = private_buttons / name
    if path.is_file():
        files[f"private_buttons/{name}"] = path
files.update({f"private_art/{path.name}": path for path in (root / "private" / "ps5").glob("*.png")})
files.update({f"game_art/{path.name}": path for path in (root / "private" / "game_art").glob("*")
              if path.is_file() and path.suffix.lower() in {".jpg", ".png"}})
files.update({f"private_art/sounds/{path.name}": path for path in (root / "private" / "ps5" / "sounds").glob("*.wav")})
era_sounds = root / "private" / "ps5" / "sounds" / "eras"
files.update({f"private_art/sounds/eras/{path.relative_to(era_sounds).as_posix()}": path
              for path in era_sounds.rglob("*") if path.is_file()})
# The owner's avatar, rendered by tools/render_avatar.py into private/ps5/avatar/.
files.update({f"private_art/avatar/{path.name}": path for path in (root / "private" / "ps5" / "avatar").glob("avatar_*")
              if path.suffix.lower() in {".frames", ".png"}})
# The avatar editor's kit (CC0 parts by Quaternius; assets/avatar/README.md).
kit = root / "assets" / "avatar" / "kit.nxak"
if kit.is_file():
    files["avatar/kit.nxak"] = kit
# Your private 360 avatar kit (tools/build_avatar_kit360.py), used instead of the CC0 kit when present.
kit360 = root / "private" / "avatar" / "kit360.nxak"
if kit360.is_file():
    files["private_art/avatar/kit360.nxak"] = kit360
# The 360's typeface when the owner has a copy (private/ps5/fonts/), else the app uses Inter.
files.update({f"private_art/fonts/{path.name}": path for path in (root / "private" / "ps5" / "fonts").glob("*")
              if path.suffix.lower() in {".ttf", ".otf", ".xtt"}})
for source_dir, target_dir in (("promotions","promotions"),("era-skins","era-skins")):
    folder = root / "private" / source_dir
    files.update({f"{target_dir}/{path.relative_to(folder).as_posix()}": path
                  for path in folder.rglob("*") if path.is_file()})
trailer_root = root / "private" / "trailers"
manifest = trailer_root / "manifest.tsv"
if manifest.is_file():
    files["trailers/manifest.tsv"] = manifest
    files.update({
        f"trailers/packed/{path.name}": path
        for path in (trailer_root / "packed").glob("*")
        if path.is_file() and path.suffix.lower() in {".frames", ".wav"}
    })
retail_library = root / "private" / "xbox" / "unpacked"
if retail_library.is_dir():
    files.update({
        f"private_library/{path.relative_to(retail_library).as_posix()}": path
        for path in retail_library.rglob("*")
        if path.is_file() and path.suffix.lower() in {".png", ".jpg", ".jpeg"}
    })
# Differing dash.xex art from tools/build_era_library.py; manifest stays private/local.
era_library = root / "private" / "era-library"
for build in ("6770", "13604", "15574", "17559"):
    files.update({
        f"private_library/eras/{path.relative_to(era_library).as_posix()}": path
        for path in (era_library / build).rglob("*")
        if path.is_file() and path.suffix.lower() in {".png", ".jpg", ".jpeg"}
    })
shared_library = root / "private" / "xbox" / "shrdres"
if shared_library.is_dir():
    files.update({
        f"private_library/shared/{path.relative_to(shared_library).as_posix()}": path
        for path in shared_library.rglob("*")
        if path.is_file() and path.suffix.lower() in {".png", ".jpg", ".jpeg"}
    })
# The owner's songs, folders and all, go where the Music Library looks. Only
# with --music, since a music folder can be large. Absolute names leave the app folder.
music_root = root / "private" / "music"
if args.music and music_root.is_dir():
    files.update({
        f"/data/nxe/music/{path.relative_to(music_root).as_posix()}": path
        for path in music_root.rglob("*")
        if path.is_file() and path.suffix.lower() in {".mp3", ".flac", ".ogg", ".oga", ".wav"}
    })
# AV2 is opt-in and independent of the dashboard build. Verification precedes FTP.
if args.avatar_editor:
    from tools.avatar_editor.deployment import avatar_editor_files
    files.update(avatar_editor_files(root / "private" / "avatar-editor"))
if args.no_elf:
    files.pop("eboot.next", None)


def remote_path(name):
    return name if name.startswith("/") else f"/data/homebrew/ExBox360/{name}"

with FTP(timeout=60) as ftp:
    ftp.connect(host, 2121)
    ftp.login("anonymous", "codex")
    directories = {"/data/homebrew/ExBox360"}
    for name in files:
        directory = ""
        for part in PurePosixPath(remote_path(name)).parts[1:-1]:
            directory += "/" + part
            directories.add(directory)
    for directory in sorted(directories, key=lambda path: path.count("/")):
        try:
            ftp.mkd(directory)
        except Exception:
            pass
    ftp.voidcmd("TYPE I")
    skipped = uploaded = left = 0
    for name, path in files.items():
        if not path.is_file():
            raise FileNotFoundError(path)
        remote = remote_path(name)
        size = path.stat().st_size
        if args.resume and name != "eboot.next":
            try:
                # ftpsrv answers 2^64-1 for a missing file, so only an exact match skips.
                if ftp.size(remote) == size:
                    skipped += 1
                    continue
            except Exception:
                pass
        if args.budget and time.monotonic() - started > args.budget:
            left += 1
            continue
        partial = remote + ".part"
        with path.open("rb") as source:
            ftp.storbinary(f"STOR {partial}", source)
        # Written under a temporary name, then renamed: an interrupted upload never
        # leaves a cut-short file under the real name.
        try:
            ftp.delete(remote)
        except Exception:
            pass
        ftp.rename(partial, remote)
        uploaded += 1
        print(f"Uploaded {name} ({size} bytes)", flush=True)
print(f"uploaded {uploaded}, already there {skipped}, left for the next run {left}")
if "eboot.next" in files and not left:
    print("The ELF is staged as eboot.next. Activate it when Ex-Box 360 is closed.")
