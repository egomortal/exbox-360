"""The launch helper (companion): privileged code the user builds on their own PC.

The helper is what lets Ex-Box open the Avatar Editor and Xbox 360 games itself, and come back.
It is privileged code, so Ex-Box Setup never builds, downloads or ships it. What Setup does instead:
find the helper's SOURCE in your folder, show the exact commands to build it on your own PC, and then
install the file you built. Building needs the PS5 payload SDK (the same toolchain the Avatar Editor
build uses). Setup does not run the compiler for you.
"""
from __future__ import annotations

from pathlib import Path

# The build output, and any already-built helper the installer will accept.
BUILT_NAMES = ("nxe_dashboard_daemon.elf", "exbox360-daemon.elf")
OUTPUT = "build/nxe_dashboard_daemon.elf"


def is_source(folder: Path) -> bool:
    """True when a Makefile in this folder builds the daemon from main.c (no code is read or run)."""
    makefile = folder / "Makefile"
    if not makefile.is_file() or not (folder / "main.c").is_file():
        return False
    try:
        text = makefile.read_text(errors="ignore")
    except OSError:
        return False
    return any(name in text for name in BUILT_NAMES)


def built_elf(source: Path) -> Path | None:
    """The helper the user has already built, if it is sitting where `make` leaves it."""
    candidate = Path(source) / OUTPUT
    if candidate.is_file():
        try:
            with candidate.open("rb") as stream:
                if stream.read(4) == b"\x7fELF":
                    return candidate
        except OSError:
            return None
    return None


def instructions(source: Path) -> dict:
    """What to show the user so they can build the helper themselves."""
    source = Path(source)
    return dict(
        folder=str(source),
        commands=[f'cd "{source}"', "make"],
        output=str(source / OUTPUT),
        needs="the PS5 payload SDK (the same build tools the Avatar Editor build uses)",
        note="Ex-Box Setup does not build this for you. The launch helper is privileged code, so you "
             "build it on your own PC with the two commands above. When it finishes, press "
             "“Look inside” again and Setup installs the file you built.",
    )
