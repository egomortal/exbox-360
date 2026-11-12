"""Ex-Box Setup entry point: this is what PyInstaller turns into the one-file app (ExBox-Setup.exe).

  ExBox-Setup                 opens the Setup window
  ExBox-Setup --cli ...       command line (same as `python -m installer ...`, see installer/__main__.py)

Packaging copied from Omnibloom Setup (outputs/omnibloom-setup), adapted: Ex-Box Setup finds the
toolchain, the editor builder and the launch helper by their place next to it in the repository, so
the app carries those folders with the same layout and imports Setup from them (not from the frozen
module archive), keeping every relative path the same as in a checkout.
"""
import importlib
import os
import sys

if getattr(sys, "frozen", False):
    ROOT = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
else:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "dashboard"))


def _windowed_output(cli):
    """The windowed exe has no console: attach to the one it was started from, or log to a file."""
    if sys.stdout is not None:
        return
    if cli and sys.platform == "win32":
        try:
            import ctypes
            if ctypes.windll.kernel32.AttachConsole(-1):
                sys.stdout = sys.stderr = open("CONOUT$", "w", encoding="utf-8", errors="replace")
                return
        except Exception:
            pass
    base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    folder = os.path.join(base, "ExBox360-Setup")
    os.makedirs(folder, exist_ok=True)
    sys.stdout = sys.stderr = open(os.path.join(folder, "setup-app.log"), "a", encoding="utf-8")


def main():
    args = sys.argv[1:]
    cli = args[:1] == ["--cli"]
    if cli:
        args = args[1:]
    _windowed_output(cli)
    installer = importlib.import_module("installer.__main__")
    return installer.main(args)


if __name__ == "__main__":
    sys.exit(main())
