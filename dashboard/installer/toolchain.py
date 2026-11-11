"""Setup's side of the self-provisioned build tools (toolchain/provision.py) and the shared libc.prx.

The tools live inside WSL (default ~/.local/share/exbox-toolchain/dependencies), never in the repo.
provision() sets them up from official, pinned sources and streams progress; build_libc() makes the
PS5_Vulkan clean-room runtime (sce_module/libc.prx) once, on this PC, for both Ex-Box titles. libc.prx
is never shipped: it is always built here and must match its pinned SHA-256.
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROVISION = REPO / "toolchain" / "provision.py"
BUILDER = REPO / "avatar-editor" / "local-builder"
LIBC_SHA256 = "e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"


def wsl(path) -> str:
    """A Windows path as WSL sees it. Paths that are already Linux paths pass through."""
    text = str(path)
    if text.startswith("/"):
        return text
    resolved = Path(path).resolve()
    return "/mnt/" + resolved.drive[0].lower() + resolved.as_posix()[2:]


def _python(args: list[str]) -> list[str]:
    if sys.platform == "win32":
        return ["wsl.exe", "--exec", "python3", "-u", *args]
    return ["python3", "-u", *args]


def provision(deps: str | None = None, rexglue: Path | None = None, xex: Path | None = None,
              progress=None, log=print, runner=subprocess.Popen) -> dict:
    """Run toolchain/provision.py and return its RESULT (status ready / needs-apt /
    failed). Never raises for a tool failure; the result says what happened."""
    args = [wsl(PROVISION)]
    if deps:
        args += ["--deps", deps]
    if rexglue:
        args += ["--rexglue-source", wsl(rexglue)]
    if xex:
        args += ["--xex-tool-source", wsl(xex)]
    result = dict(status="failed", error="The tool setup did not report a result.")
    process = runner(_python(args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                     encoding="utf-8", errors="replace")
    for line in process.stdout:
        line = line.rstrip("\n")
        if line.startswith("PROGRESS "):
            parts = line.split(" ", 4)
            if len(parts) == 5 and progress:
                step, total = int(parts[1]), max(int(parts[2]), 1)
                progress(min(step / total, 1.0), parts[4])
        elif line.startswith("RESULT "):
            try:
                result = json.loads(line[7:])
            except ValueError:
                result = dict(status="failed", error="The tool setup reported an unreadable result.")
        elif line.strip() and log:
            log(line)
    process.wait()
    return result


def explain(result: dict) -> str:
    """One plain paragraph for a provisioning result that is not 'ready'."""
    status = result.get("status")
    if status == "needs-apt":
        return ("This PC needs a one-time install of some Ubuntu build packages first. Open Ubuntu (WSL) and run:\n  "
                + "\n  ".join(result.get("commands", [])) + "\nthen press Install again. " + result.get("why", ""))
    if status == "failed":
        return f"Setting up the build tools stopped at '{result.get('step', '?')}': {result.get('error', '')}"
    return ""


def build_libc(deps: str, out_dir: Path, runner=subprocess.run) -> Path:
    """Build sce_module/libc.prx on this PC with the provisioned tools; returns the checked file."""
    out_dir.mkdir(parents=True, exist_ok=True)
    work = out_dir / "libc-build"
    if work.exists():
        shutil.rmtree(work)
    script = ("import sys;sys.path.insert(0,sys.argv[1]);import companion_producer as c;"
              "print(c.build_libc(sys.argv[2],sys.argv[3],sys.argv[4],sys.argv[5]))")
    argv = _python(["-c", script, wsl(BUILDER), f"{deps}/sources/vulkan-port-source",
                    f"{deps}/llvm21/bin/clang++", f"{deps}/native-tools/ps5-native-tool", wsl(work)])
    result = runner(argv, capture_output=True, text=True)
    if result.returncode:
        tail = (result.stdout + result.stderr).strip().splitlines()[-3:]
        raise RuntimeError("Building libc.prx on this PC failed: " + " / ".join(tail))
    built = work / "libc-a.prx"
    if not built.is_file() or hashlib.sha256(built.read_bytes()).hexdigest() != LIBC_SHA256:
        raise RuntimeError("The libc.prx built on this PC does not match its pinned SHA-256; it was not used.")
    return built
