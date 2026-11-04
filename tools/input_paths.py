"""Portable locations for user-supplied original inputs (no downloads or writes)."""
from pathlib import Path
import os


def input_root(project_root: Path, environ=None) -> Path:
    """Default to this checkout's inputs/; an explicit override works on WSL too."""
    environ = os.environ if environ is None else environ
    value = environ.get("EXBOX_INPUTS")
    return Path(value).expanduser().resolve() if value else project_root / "inputs"


def dashboard_resources(project_root: Path, environ=None) -> Path:
    return input_root(project_root, environ) / "dashboard-resources"


def prepared_editor(project_root: Path, environ=None) -> Path:
    return input_root(project_root, environ) / "avatar-editor"
