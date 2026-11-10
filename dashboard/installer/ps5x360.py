"""Optional PS5X360 (Xbox 360 games) by BrinooTk, MIT licence. Third-party; we never change or rehost it.

The installer fetches one pinned file from the official GitHub release, checks its size and SHA-256,
and installs it as it is, including its LICENSE and CREDITS files. It never fetches games, BIOS or keys.
To move to a newer PS5X360, change VERSION, SIZE and SHA256 together (the SHA-256 is shown on the
release page next to the file).
"""
from __future__ import annotations

import hashlib
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

AUTHOR = "BrinooTk"
LICENCE = "MIT"
VERSION = "v1.0.2"
ASSET = "PPSA50011.zip"
SIZE = 86_415_109
SHA256 = "230359c2638f99add3972147e1690e62d8984a37c00181501680bdda7c1096c8"
PAGE = "https://github.com/BrinooTk/PS5X360/releases/tag/" + VERSION
URL = f"https://github.com/BrinooTk/PS5X360/releases/download/{VERSION}/{ASSET}"
TITLE_ID = "PPSA50011"
MAX_UNPACKED = 2 * 1024 ** 3


def check(path: Path) -> None:
    """Raise ValueError unless the file is exactly the pinned release file."""
    if path.stat().st_size != SIZE:
        raise ValueError("The PS5X360 download is the wrong size. It may be incomplete; try again.")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    if digest.hexdigest() != SHA256:
        raise ValueError("The PS5X360 download did not pass its safety check (SHA-256). It was not installed.")


def fetch(cache_dir: Path, progress=None, opener=urllib.request.urlopen) -> Path:
    """Download the pinned release file once into cache_dir and check it. One request, no retries loop."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = cache_dir / f"PS5X360-{VERSION}-{ASSET}"
    if target.is_file():
        try:
            check(target)
            return target
        except ValueError:
            target.unlink()
    part = target.with_suffix(".part")
    request = urllib.request.Request(URL, headers={"User-Agent": "ExBox360-Setup"})
    done = 0
    with opener(request, timeout=60) as response, part.open("wb") as out:
        for block in iter(lambda: response.read(1 << 18), b""):
            done += len(block)
            if done > SIZE:
                break
            out.write(block)
            if progress:
                progress(done, SIZE)
    try:
        check(part)
    except ValueError:
        part.unlink(missing_ok=True)
        raise
    part.replace(target)
    return target


def members(zip_path: Path):
    """Yield (path inside the PS5X360 title folder, ZipInfo) for every file, refusing unsafe names."""
    total = 0
    with zipfile.ZipFile(zip_path) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            parts = PurePosixPath(info.filename.replace("\\", "/")).parts
            if not parts or parts[0] != TITLE_ID or ".." in parts or info.filename.startswith("/"):
                raise ValueError("The PS5X360 file has an unexpected layout. It was not installed.")
            total += info.file_size
            if total > MAX_UNPACKED:
                raise ValueError("The PS5X360 file is larger than expected. It was not installed.")
            yield "/".join(parts[1:]), info


def notice() -> str:
    return (f"PS5X360 {VERSION} by {AUTHOR}, {LICENCE} licence. Installed unchanged from its official "
            f"release ({PAGE}). Its LICENSE and CREDITS files are kept in its folder on your PS5.")
