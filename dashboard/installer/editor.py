"""Avatar Editor: run the existing local builder with no windows or prompts, or install a finished build.

The builder (avatar-editor/local-builder/builder.py) already takes everything on its command line:
    builder.py --inputs ORIGINALS --dependencies TOOLS --work FRESH_FOLDER
It needs Linux (WSL on Windows) and the tools listed in its README; this module only starts it,
relays its progress lines and picks up the finished package. It downloads and installs nothing.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath

BUILDER = Path(__file__).resolve().parents[2] / "avatar-editor" / "local-builder"
PACKAGE = "native-work/package"
ORIGINALS = ("avatareditor.xex", "xenonclatin.xtt", "splash_360.png")


def _wsl(path) -> str:
    """A Windows path as WSL sees it. Linux paths (the provisioned tools in WSL) pass through."""
    if str(path).startswith("/"):
        return str(path)
    resolved = Path(path).resolve()
    return "/mnt/" + resolved.drive[0].lower() + resolved.as_posix()[2:]


def command(originals: Path, dependencies, work) -> list[str]:
    if sys.platform == "win32":
        return ["wsl.exe", "--exec", "python3", "-u", _wsl(BUILDER / "builder.py"), "--inputs", _wsl(originals),
                "--dependencies", _wsl(dependencies), "--work", _wsl(work)]
    return [sys.executable, "-u", str(BUILDER / "builder.py"), "--inputs", str(originals),
            "--dependencies", str(dependencies), "--work", str(work)]


def build(originals: dict, dependencies, work: Path, log=print, progress=None, runner=subprocess.Popen,
          linux_work: str | None = None, copier=subprocess.run) -> Path:
    """Build the editor from the user's three files. Returns the finished package ZIP.

    dependencies may be a Linux path (the provisioned tools in WSL). linux_work, when given, is a
    fresh Linux folder where the 1,200-unit compile runs (much faster than the Windows drive); the
    finished package is then copied back into `work`."""
    missing = [name for name in ORIGINALS if name not in originals]
    if missing:
        raise RuntimeError("The Avatar Editor build needs: " + ", ".join(missing))
    if sys.platform == "win32" and not shutil.which("wsl.exe"):
        raise RuntimeError("The Avatar Editor build needs WSL (Windows Subsystem for Linux), which was not found.")
    work.mkdir(parents=True, exist_ok=False)
    staged = work / "originals"
    staged.mkdir()
    for name in ORIGINALS:  # copies; the user's own files are never changed
        shutil.copyfile(originals[name], staged / name)
    out = linux_work or (work / "out")
    process = runner(command(staged, dependencies, out), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                     text=True, encoding="utf-8", errors="replace")
    lines = 0
    with (work / "build.log").open("w", encoding="utf-8") as record:
        for line in process.stdout:
            record.write(line)
            lines += 1
            text = line.strip()
            if text and progress:
                progress(min(lines / 4000, 0.95), text[:90])
    if process.wait():
        raise RuntimeError(f"The Avatar Editor build stopped with a problem. Its log is {work / 'build.log'}")
    if linux_work:
        copied = work / "editor-package.zip"
        find = (f'cp "$(ls -1 {linux_work}/build-*/{PACKAGE}/*.zip | tail -n 1)" "{_wsl(copied)}"')
        argv = (["wsl.exe", "--exec", "sh", "-c", find] if sys.platform == "win32" else ["sh", "-c", find])
        if copier(argv).returncode or not copied.is_file():
            raise RuntimeError("The Avatar Editor was built, but its package could not be copied back.")
        log("Avatar Editor built on this PC.")
        return copied
    packages = sorted(Path(out).glob("build-*/" + PACKAGE + "/*.zip"))
    if not packages:
        raise RuntimeError("The Avatar Editor build finished without a package.")
    log("Avatar Editor built on this PC.")
    return packages[-1]


def package_files(package: Path, remote_root: str, source_type) -> dict:
    """Files of a finished editor (a PPSA99734 folder, or the builder's ZIP) mapped to the console."""
    if package.is_dir():
        return {f"{remote_root}/{p.relative_to(package).as_posix()}": source_type(p)
                for p in sorted(package.rglob("*")) if p.is_file() and not p.is_symlink()}
    files = {}
    with zipfile.ZipFile(package) as archive:
        infos = [i for i in archive.infolist() if not i.is_dir()]
        names = [PurePosixPath(i.filename.replace("\\", "/")) for i in infos]
        eboots = [n for n in names if n.name == "eboot.bin"]
        if not eboots:
            raise RuntimeError("That Avatar Editor package has no eboot.bin.")
        base = min(eboots, key=lambda n: len(n.parts)).parent
        for info, name in zip(infos, names):
            if ".." in name.parts or name.is_absolute():
                raise RuntimeError("That Avatar Editor package has an unsafe file name.")
            try:
                relative = name.relative_to(base)
            except ValueError:
                continue
            files[f"{remote_root}/{relative.as_posix()}"] = source_type(zip_path=package, member=info)
    return files
