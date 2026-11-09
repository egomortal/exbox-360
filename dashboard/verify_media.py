"""Compatibility entry point for the native media verifier."""
from pathlib import Path
import runpy
if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("verify_native_media.py")), run_name="__main__")
