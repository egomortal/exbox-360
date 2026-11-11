"""The installer window: a small web page served only to this PC (127.0.0.1), opened in the browser.

Standard library only. Each window gets a random key, so no other web page can drive the installer.
"""
from __future__ import annotations

import json
import secrets
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from . import VERSION, catalog, engine, ps5x360
from .targets import FtpTarget, discover

PAGE = Path(__file__).resolve().parent / "ui.html"
PAYLOAD = Path(__file__).resolve().parent / "payload"


class State:
    def __init__(self):
        self.key = secrets.token_urlsafe(24)
        self.scan = None
        self.job = None
        self.lock = threading.Lock()


def pick_folder() -> str:
    """Show the normal Windows "choose a folder" box and return the choice ('' if cancelled)."""
    if sys.platform == "win32":
        script = ("Add-Type -AssemblyName System.Windows.Forms;"
                  "$d=New-Object System.Windows.Forms.FolderBrowserDialog;"
                  "$d.Description='Pick the folder that holds your Xbox 360 files';"
                  "$d.ShowNewFolderButton=$false;"
                  "$o=New-Object System.Windows.Forms.Form;$o.TopMost=$true;"
                  "if($d.ShowDialog($o) -eq 'OK'){[Console]::OutputEncoding=[Text.Encoding]::UTF8;$d.SelectedPath}")
        result = subprocess.run(["powershell.exe", "-NoProfile", "-STA", "-Command", script],
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        return result.stdout.strip()
    try:
        import tkinter
        from tkinter import filedialog
        root = tkinter.Tk()
        root.withdraw()
        path = filedialog.askdirectory(title="Pick the folder that holds your Xbox 360 files")
        root.destroy()
        return path or ""
    except Exception:
        return ""


def open_target(body: dict):
    if body.get("mode") == "console":
        host = str(body.get("host", "")).strip()
        if not host:
            raise ValueError("Type your PS5's address first (for example 192.168.1.50).")
        try:
            return FtpTarget(host, int(body.get("port") or 2121), timeout=20)
        except OSError:
            raise ValueError(f"No PS5 answered at {host}. Check that it is on, on the same network, and "
                             "that its FTP service is running.")
    return engine.practice_target()


class Job:
    def __init__(self):
        self.progress = dict(step=0, steps=1, title="Starting", fraction=0.0, detail="")
        self.log: list[str] = []
        self.result = None
        self.installer = None

    def view(self) -> dict:
        return dict(progress=self.progress, log=self.log[-200:], result=self.result, running=self.result is None)


def start_install(state: State, body: dict) -> Job:
    if state.scan is None:
        raise ValueError("Pick your folder first.")
    job = Job()
    target = open_target(body)
    options = dict(icons=body.get("icons", "xbox"), ps5x360=bool(body.get("ps5x360", True)),
                   companion=bool(body.get("companion", True)))
    job.installer = engine.Installer(target, state.scan, options, log=job.log.append,
                                     progress=lambda event: job.progress.update(event))

    def work():
        try:
            job.result = job.installer.run()
        except Exception as error:
            job.result = dict(ok=False, summary=f"The install stopped: {error}", steps=[], practice=target.practice)
        finally:
            target.close()
    threading.Thread(target=work, daemon=True).start()
    return job


def api(state: State, name: str, body: dict) -> dict:
    if name == "pick-folder":
        return dict(folder=pick_folder())
    if name == "scan":
        result = catalog.scan(body.get("folder", ""), catalog.Hasher(engine.home() / "hash-cache.json"), [PAYLOAD])
        state.scan = result
        news = []
        if body.get("mode") != "console":  # the console is only contacted when the user presses a console button
            news = catalog.new_since(result, engine.load_record(engine.practice_target()))
        view = {k: result[k] for k in ("folder", "files_seen", "wrong", "preview")}
        view["items"] = [dict(item) for item in result["items"]]  # copies: the scan itself stays whole
        view.update(new_since=news, has_loading=bool(result["sources"]["loading"]),
                    has_companion=bool(result["sources"]["companion"]))
        for item in view["items"]:
            item["files"] = len(item["files"])
        return view
    if name == "install":
        with state.lock:
            if state.job and state.job.result is None:
                raise ValueError("An install is already running.")
            state.job = start_install(state, body)
        return dict(started=True)
    if name == "progress":
        return state.job.view() if state.job else dict(running=False, result=None, log=[], progress=None)
    if name == "stop":
        if state.job and state.job.installer:
            state.job.installer.cancel = True
        return dict(stopping=True)
    if name == "find-console":
        return dict(hosts=discover())
    if name in ("doctor", "restore"):
        with open_target(body) as target:
            return engine.doctor(target) if name == "doctor" else engine.restore(target)
    if name == "open-link":
        # Only the official project pages from the pinned list; the page never supplies a web address.
        url = catalog.project_links().get(str(body.get("id", "")))
        if not url:
            raise ValueError("There is no project page for that item.")
        webbrowser.open(url)
        return dict(opened=url)
    if name == "info":
        return dict(version=VERSION, practice_folder=str(engine.practice_target().root),
                    ps5x360=dict(version=ps5x360.VERSION, author=ps5x360.AUTHOR, licence=ps5x360.LICENCE,
                                 page=ps5x360.PAGE))
    raise ValueError("Unknown request")


def make_handler(state: State):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def _send(self, code, body: bytes, kind="application/json"):
            self.send_response(code)
            self.send_header("Content-Type", kind + "; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(body)

        def _local(self) -> bool:
            host = (self.headers.get("Host") or "").split(":")[0]
            return host in ("127.0.0.1", "localhost")

        def do_GET(self):
            if not self._local() or self.path.split("?")[0] != "/":
                return self._send(404, b"{}")
            page = PAGE.read_text(encoding="utf-8").replace("__KEY__", state.key).replace("__VERSION__", VERSION)
            self._send(200, page.encode("utf-8"), "text/html")

        def do_POST(self):
            if not self._local() or self.headers.get("X-ExBox-Key") != state.key \
                    or not self.path.startswith("/api/"):
                return self._send(403, b'{"error":"not allowed"}')
            try:
                length = int(self.headers.get("Content-Length") or 0)
                body = json.loads(self.rfile.read(min(length, 1 << 20)) or b"{}")
                self._send(200, json.dumps(api(state, self.path[5:], body)).encode())
            except ValueError as error:
                self._send(400, json.dumps(dict(error=str(error))).encode())
            except Exception as error:
                self._send(500, json.dumps(dict(error=f"Something went wrong: {error}")).encode())
    return Handler


def serve(port: int = 0, browser: bool = True) -> int:
    state = State()
    httpd = ThreadingHTTPServer(("127.0.0.1", port), make_handler(state))
    url = f"http://127.0.0.1:{httpd.server_address[1]}/"
    print(f"Ex-Box Setup is open at {url}\nLeave this window open while you use it. Close it to quit.", flush=True)
    if browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0
