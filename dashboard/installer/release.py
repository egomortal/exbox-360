"""The Ex-Box 360 dashboard app (PPSA99735): our own code, which may ship prebuilt.

Binaries never go in the source repo (no binaries in git). For a public release the dashboard ships
one of two ways, and Setup handles both:

  * Bundled in the Windows download, unpacked at installer/payload/PPSA99735/. Setup uses it straight
    away, with no network. The download's build step copies the built title there; git ignores it.
  * As a pinned asset on the Ex-Box GitHub release. Setup downloads it and checks it by SHA-256, the
    same safe mechanism PS5X360 uses. Fill RELEASE below when you tag the release.

Until a release is pinned and no payload is bundled, Setup says the dashboard app will come from the
Ex-Box release and installs everything else (keeping Ex-Box if it is already on the PS5).
"""
from __future__ import annotations

import hashlib
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

PAYLOAD = Path(__file__).resolve().parent / "payload"
TITLE_ID = "PPSA99735"
MAX_UNPACKED = 512 * 1024 ** 2

# Set this when you tag a release and upload the dashboard title as a zip asset to it.
# RELEASE = dict(tag="v1.0", asset="ExBox360-PPSA99735.zip", size=12345678, sha256="<64 hex chars>")
RELEASE: dict | None = None
REPO = "egomortal/exbox-360"


def configured() -> bool:
    return bool(RELEASE and RELEASE.get("sha256") and RELEASE.get("size"))


def url() -> str:
    return f"https://github.com/{REPO}/releases/download/{RELEASE['tag']}/{RELEASE['asset']}"


def local_package() -> Path | None:
    """The dashboard title bundled in this installer download, if the build put it there."""
    folder = PAYLOAD / TITLE_ID
    return folder if (folder / "eboot.bin").is_file() else None


def tag() -> str:
    return RELEASE["tag"] if RELEASE else "the Ex-Box release"


def check(path: Path) -> None:
    if path.stat().st_size != RELEASE["size"]:
        raise ValueError("The Ex-Box dashboard download is the wrong size; it may be incomplete. Try again.")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    if digest.hexdigest() != RELEASE["sha256"]:
        raise ValueError("The Ex-Box dashboard download did not pass its safety check (SHA-256); not installed.")


def _members(zip_path: Path):
    total = 0
    with zipfile.ZipFile(zip_path) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            parts = PurePosixPath(info.filename.replace("\\", "/")).parts
            if not parts or parts[0] != TITLE_ID or ".." in parts or info.filename.startswith("/"):
                raise ValueError("The Ex-Box dashboard file has an unexpected layout; not installed.")
            total += info.file_size
            if total > MAX_UNPACKED:
                raise ValueError("The Ex-Box dashboard file is larger than expected; not installed.")
            yield parts, info


def fetch(cache_dir: Path, progress=None, opener=urllib.request.urlopen) -> Path:
    """Download the pinned dashboard asset, verify it, unpack it, and return the PPSA99735 folder."""
    if not configured():
        raise ValueError("No Ex-Box dashboard release is set yet, and none was bundled with Setup.")
    cache_dir.mkdir(parents=True, exist_ok=True)
    archive = cache_dir / f"ExBox360-{RELEASE['tag']}-{RELEASE['asset']}"
    if not (archive.is_file() and _ok(archive)):
        part = archive.with_suffix(".part")
        request = urllib.request.Request(url(), headers={"User-Agent": "ExBox360-Setup"})
        done = 0
        with opener(request, timeout=60) as response, part.open("wb") as out:
            for block in iter(lambda: response.read(1 << 18), b""):
                done += len(block)
                if done > RELEASE["size"]:
                    break
                out.write(block)
                if progress:
                    progress(done, RELEASE["size"])
        try:
            check(part)
        except ValueError:
            part.unlink(missing_ok=True)
            raise
        part.replace(archive)
    out_dir = cache_dir / f"ExBox360-{RELEASE['tag']}"
    for parts, info in _members(archive):
        destination = out_dir.joinpath(*parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive) as z, z.open(info) as source, destination.open("wb") as target:
            while block := source.read(1 << 20):
                target.write(block)
    return out_dir / TITLE_ID


def _ok(path: Path) -> bool:
    try:
        check(path)
        return True
    except ValueError:
        return False
