"""Checks the built one-file app (ExBox-Setup.exe), the way Omnibloom Setup checks its own.

    python dashboard/tests/frozen_check.py dashboard/dist/ExBox-Setup.exe

Runs the app itself, never a PS5: version, a scan, a practice install into a temporary folder (with
made-up files from the installer tests), and that the window's local server starts and answers.
Everything happens in temporary folders; nothing outside them is touched.
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_installer import fake_folder  # noqa: E402  (made-up files only)


def run(app, *args, env=None, timeout=300):
    result = subprocess.run([str(app), "--cli", *args], capture_output=True, text=True, encoding="utf-8",
                            errors="replace", env=env, timeout=timeout)
    return result.returncode, result.stdout + result.stderr


def main(app):
    app = Path(app).resolve()
    failures = []
    with tempfile.TemporaryDirectory(prefix="exbox-frozen-") as tmp:
        tmp = Path(tmp)
        env = dict(os.environ, EXBOX_SETUP_HOME=str(tmp / "home"))

        code, out = run(app, "--version", env=env)
        print("version:", out.strip())
        if code:
            failures.append("--version failed")

        folder = fake_folder(tmp / "folder")
        code, out = run(app, "scan", str(folder), "--json", env=env)
        try:
            items = {i["id"]: i["status"] for i in json.loads(out[out.index("{"):])["items"]}
            print("scan:", sum(s == "found" for s in items.values()), "found of", len(items))
            if items.get("dashboard-app") != "found":
                failures.append("scan did not find the dashboard app")
        except (ValueError, KeyError) as error:
            failures.append(f"scan output unreadable: {error}: {out[-300:]}")

        practice = tmp / "practice-console"
        code, out = run(app, "install", str(folder), "--practice", "--target", str(practice), "--no-ps5x360",
                        "--no-helper", "--icons", "default", "--json", env=env)
        try:
            outcome = json.loads(out[out.index("{"):])
            print("practice install:", "ok" if outcome["ok"] else "FAILED", "-", outcome.get("summary", "")[:200])
            if not outcome["ok"]:
                failures.append("practice install failed")
        except (ValueError, KeyError) as error:
            failures.append(f"install output unreadable: {error}: {out[-300:]}")
        landed = list(practice.rglob("eboot.bin"))
        print("files on the practice console:", sum(1 for p in practice.rglob("*") if p.is_file()))
        if not landed:
            failures.append("no eboot.bin reached the practice console")

        port = 8790
        window = subprocess.Popen([str(app), "--cli", "ui", "--no-browser", "--port", str(port)], env=env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            page = None
            for _ in range(60):
                try:
                    page = urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=2).read()
                    break
                except OSError:
                    time.sleep(1)
            print("window server:", "answers" if page else "no answer")
            if not page or b"Ex-Box" not in page:
                failures.append("the window's local server did not serve the Setup page")
        finally:
            # A one-file app is two processes (unpacker + app); stop both, or the app keeps the
            # port open and locks the .exe against the next build.
            if sys.platform == "win32":
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(window.pid)], capture_output=True)
            else:
                window.terminate()
            window.wait(timeout=30)

    print("RESULT:", "PASS" if not failures else "FAIL: " + "; ".join(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "dashboard/dist/ExBox-Setup.exe"))
