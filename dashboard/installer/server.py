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

from . import VERSION, catalog, engine, linksource, ps5x360
from .targets import FtpTarget, discover

PAGE = Path(__file__).resolve().parent / "ui.html"
PAYLOAD = Path(__file__).resolve().parent / "payload"


class State:
    def __init__(self):
        self.key = secrets.token_urlsafe(24)
        self.scan = None
        self.job = None
        self.lock = threading.Lock()
        self._links = None  # files added from links; kept in a temporary folder, never the links
        self.link_job = None  # the download in progress, if any

    @property
    def links(self) -> linksource.Links:
        if self._links is None:
            self._links = linksource.Links()
        return self._links

    def close(self):
        if self.link_job and self.link_job.thread.is_alive():
            self.link_job.cancel.set()
            self.link_job.thread.join(30)
        if self._links is not None:
            self._links.close()
            self._links = None


class LinkJob:
    """One download running in the background, so the window can show progress and offer Cancel."""

    def __init__(self, links, url: str):
        self.cancel = threading.Event()
        self.step, self.done, self.total = "starting", 0, None
        self.result, self.error = None, None
        self.thread = threading.Thread(target=self._run, args=(links, url), daemon=True)
        self.thread.start()

    def _progress(self, step, done, total):
        self.step, self.done, self.total = step, done, total

    def _run(self, links, url):
        try:
            self.result = links.add(url, True, progress=self._progress, cancel=self.cancel)
        except linksource.LinkError as error:
            self.error = str(error)  # its messages never contain the link
        except Exception:
            # Anything unexpected is reported without its text, which could repeat the link.
            self.error = "That link could not be used. Nothing from it was kept."

    def view(self) -> dict:
        return dict(running=self.thread.is_alive(), step=self.step, done=self.done, total=self.total,
                    added=self.result, error=self.error)


def pick_zip() -> str:
    """Show the normal Windows "open file" box for one ZIP file ('' if cancelled or not on Windows)."""
    if sys.platform != "win32":
        return ""
    script = ("Add-Type -AssemblyName System.Windows.Forms;"
              "$d=New-Object System.Windows.Forms.OpenFileDialog;"
              "$d.Title='Pick a ZIP file that holds your Xbox 360 files';"
              "$d.Filter='ZIP files (*.zip)|*.zip';$d.Multiselect=$false;"
              "$o=New-Object System.Windows.Forms.Form;$o.TopMost=$true;"
              "if($d.ShowDialog($o) -eq 'OK'){[Console]::OutputEncoding=[Text.Encoding]::UTF8;$d.FileName}")
    result = subprocess.run(["powershell.exe", "-NoProfile", "-STA", "-Command", script],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    return result.stdout.strip()


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
    if name == "pick-zip":
        return dict(folder=pick_zip())
    if name == "add-link":
        if state.job and state.job.result is None:
            raise ValueError("Wait until the install has finished before adding files.")
        if state.link_job and state.link_job.thread.is_alive():
            raise ValueError("A download is already running. Wait for it or press Cancel.")
        if body.get("consent") is not True:
            raise linksource.LinkError("Tick \"" + linksource.CONSENT_TEXT + "\" first. Nothing was downloaded.")
        url = str(body.get("url", ""))
        linksource.check_url(url, state.links.opener.resolve)  # refuse a bad link before starting
        state.link_job = LinkJob(state.links, url)
        return dict(started=True)
    if name == "link-progress":
        view = state.link_job.view() if state.link_job else dict(running=False, added=None, error=None)
        view["links"] = state.links.summary() if state._links is not None else []
        return view
    if name == "cancel-link":
        if state.link_job:
            state.link_job.cancel.set()
        return dict(cancelling=True)
    if name == "clear-links":
        if state.job and state.job.result is None:
            raise ValueError("Wait until the install has finished.")
        state.close()
        state.link_job = None
        return dict(links=[])
    if name == "scan":
        if state.link_job and state.link_job.thread.is_alive():
            raise ValueError("Wait for the download to finish, or press Cancel.")
        result = catalog.scan_all(body.get("folder", "") or None, state.links,
                                  catalog.Hasher(engine.home() / "hash-cache.json"), [PAYLOAD])
        state.scan = result
        news = []
        if body.get("mode") != "console":  # the console is only contacted when the user presses a console button
            news = catalog.new_since(result, engine.load_record(engine.practice_target()))
        view = {k: result[k] for k in ("folder", "files_seen", "wrong", "preview")}
        view["links"] = state.links.summary() if state._links is not None else []
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
        state.close()  # files added from links are deleted when Setup closes
    return 0
