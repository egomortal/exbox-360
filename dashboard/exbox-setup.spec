# PyInstaller spec: Ex-Box Setup as one file.
#     pyinstaller dashboard/exbox-setup.spec        (run from the repository root)
# Copied from Omnibloom Setup's spec and adapted. Setup's own code and the folders it uses at run time
# go in as plain files with the repository layout (see run_setup.py); only files git tracks are
# taken, so nothing private, generated or user-supplied can end up in the app.
import subprocess
import sys
from pathlib import Path

ROOT = Path(SPECPATH).resolve().parent  # noqa: F821 (SPECPATH is set by PyInstaller)

# Everything Setup reads or runs at run time, by repository path.
FOLDERS = [
    "dashboard/installer",           # Setup itself (code + ui.html)
    "dashboard/tools",               # branding, icon drawing and the art pipeline
    "dashboard/installer-icons",     # Ex-Box's own default icons (branding)
    "toolchain",                     # provision.py + pins (runs in WSL)
    "avatar-editor/local-builder",   # the editor builder (runs in WSL)
    "launch-helper",                 # source Setup builds the launch helper from
]
FILES = ["dashboard/setup.py"]
SKIP = ("/tests/", "/test_", "/__pycache__/")


def tracked(path):
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "--", path],
                         capture_output=True, check=True).stdout.decode()
    return [p for p in out.split("\0") if p]


datas = []
for item in FOLDERS + FILES:
    for rel in tracked(item):
        if any(s in "/" + rel for s in SKIP) and not rel.startswith("avatar-editor/"):
            continue  # Setup's own tests stay out; the builder keeps all its files (it checks them)
        datas.append((str(ROOT / rel), str(Path(rel).parent)))
if not any(d[0].endswith("ui.html") for d in datas):
    sys.exit("Setup's files were not found; run this from a git checkout.")

# Setup is imported from the bundled files at run time, so its imports are listed here.
hidden = ["argparse", "concurrent.futures", "ftplib", "hashlib", "http.server", "io", "json", "secrets",
          "shutil", "socket", "sqlite3", "stat", "subprocess", "tempfile", "threading", "urllib.request",
          "webbrowser", "zipfile", "PIL", "PIL.Image", "PIL.ImageDraw",
          "PIL.ImageFont", "PIL.ImageFilter", "PIL.ImageOps", "PIL.ImageChops", "ctypes"]

a = Analysis(
    [str(ROOT / "dashboard" / "run_setup.py")],
    pathex=[str(ROOT / "dashboard")],
    datas=datas,
    hiddenimports=hidden,
    # installer: never from the module archive (run_setup.py imports the bundled files).
    # tkinter: only a non-Windows fallback for the folder picker; on Windows Setup uses the system
    # dialog, and PyInstaller 6.16 cannot bundle Python 3.14's Tk 9 yet.
    excludes=["installer", "tkinter", "_tkinter"],
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    name="ExBox-Setup",
    console=True,  # Setup's window is a browser page; this small console is how you close Setup
    upx=False,
)
