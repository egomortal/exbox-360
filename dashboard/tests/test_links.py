"""Checks for "Add files from a link" (installer/linksource.py). No network is used.

Every download is served by an in-process fake HTTPS handler and every address lookup by a fake
resolver, so nothing leaves this PC. Test links are built at run time from random tokens under the
reserved .invalid domain: no real site and no suggested source appears here.
"""
import email.message
import hashlib
import io
import json
import os
import secrets
import shutil
import socket
import stat
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
import urllib.response
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from installer import __main__ as cli, catalog, engine, linksource, server, targets  # noqa: E402
from test_installer import fake_folder  # noqa: E402  (made-up files only)

PUBLIC = "1.2.3.4"   # stands in for a public address; nothing ever connects to it
PRIVATE = {"lan": "192.168.1.20", "loop": "127.0.0.1", "ten": "10.1.2.3", "link": "169.254.1.1", "v6": "::1"}


def host(tag=""):
    return f"{tag}{secrets.token_hex(4)}.invalid"


def link(hostname, path="/f"):
    return urllib.parse.urlunsplit(("https", hostname, path, "", ""))


class FakeWeb(urllib.request.BaseHandler):
    """Answers https requests from a table: {url: (status, headers, body)}. Counts every request."""
    handler_order = 100  # before the real HTTPS handler, which is never reached

    def __init__(self):
        self.routes, self.requests = {}, []

    def https_open(self, req):
        self.requests.append(req.full_url)
        status, headers, body = self.routes.get(req.full_url, (404, {}, b""))
        message = email.message.Message()
        for k, v in headers.items():
            message[k] = v
        if "Content-Length" not in headers and body is not None:
            message["Content-Length"] = str(len(body))
        response = urllib.response.addinfourl(io.BytesIO(body or b""), message, req.full_url, status)
        response.msg = "fake"
        return response


class Resolver:
    """Host -> addresses. Unknown hosts get the public stand-in address."""

    def __init__(self):
        self.table = {}

    def __call__(self, hostname, port, proto=0, **_):
        address = self.table.get(hostname, PUBLIC)
        family = socket.AF_INET6 if ":" in address else socket.AF_INET
        return [(family, socket.SOCK_STREAM, proto, "", (address, port))]


def zip_bytes(files: dict, symlinks=()) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, data)
        for name in symlinks:
            info = zipfile.ZipInfo(name)
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, "target")
    return buffer.getvalue()


def folder_zip(folder: Path) -> bytes:
    return zip_bytes({p.relative_to(folder).as_posix(): p.read_bytes() for p in folder.rglob("*") if p.is_file()})


class LinkBase(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        os.environ["EXBOX_SETUP_HOME"] = str(self.tmp / "home")
        self.web, self.resolve = FakeWeb(), Resolver()
        self.opener = linksource.make_opener(self.resolve, self.web)
        self.links = linksource.Links(self.opener, temp_root=self.tmp)
        self.addCleanup(self.links.close)

    def serve(self, url, body, status=200, headers=None):
        self.web.routes[url] = (status, headers or {}, body)
        return url


class Validation(LinkBase):
    def test_https_to_a_public_address_is_accepted(self):
        linksource.check_url(link(host()), self.resolve)

    def test_other_schemes_are_refused(self):
        h = host()
        for scheme in ("http", "ftp", "file", "data", "javascript", "HTTP"):
            with self.subTest(scheme=scheme), self.assertRaises(linksource.LinkError):
                linksource.check_url(urllib.parse.urlunsplit((scheme, h, "/f", "", "")), self.resolve)

    def test_this_pc_and_the_local_network_are_refused(self):
        for literal in ("127.0.0.1", "10.0.0.1", "192.168.0.10", "172.16.5.5", "169.254.9.9", "100.64.0.1",
                        "0.0.0.0", "224.0.0.1", "[::1]", "[fc00::1]", "[fe80::1]", "[::ffff:192.168.0.1]"):
            with self.subTest(address=literal), self.assertRaises(linksource.LinkError):
                linksource.check_url(link(literal), self.resolve)
        for name in ("localhost", "box.local", "nas.lan", "x.localhost", "router.home", "a.internal"):
            with self.subTest(name=name), self.assertRaises(linksource.LinkError):
                linksource.check_url(link(name), self.resolve)

    def test_a_name_that_resolves_to_a_private_address_is_refused(self):
        for tag, address in PRIVATE.items():
            h = host(tag)
            self.resolve.table[h] = address
            with self.subTest(address=address), self.assertRaises(linksource.LinkError):
                linksource.check_url(link(h), self.resolve)

    def test_names_and_passwords_in_links_are_refused(self):
        with self.assertRaises(linksource.LinkError):
            linksource.check_url("https://user:secret@" + host() + "/f", self.resolve)

    def test_errors_never_repeat_the_link(self):
        secret = secrets.token_hex(8)
        for url in ("http://" + host() + "/" + secret, link(host(), "/" + secret), "not a link " + secret):
            try:
                self.links.add(url, True)
            except linksource.LinkError as error:
                self.assertNotIn(secret, str(error))


class Redirects(LinkBase):
    def test_a_redirect_to_plain_http_is_refused(self):
        start = self.serve(link(host()), b"", 302, {"Location": "http://" + host() + "/f"})
        with self.assertRaises(linksource.LinkError):
            self.links.add(start, True)
        self.assertEqual(len(self.web.requests), 1)  # the http target was never requested

    def test_a_redirect_to_the_local_network_is_refused(self):
        inside = host("lan")
        self.resolve.table[inside] = PRIVATE["lan"]
        start = self.serve(link(host()), b"", 302, {"Location": link(inside)})
        with self.assertRaises(linksource.LinkError):
            self.links.add(start, True)
        self.assertNotIn(link(inside), self.web.requests)

    def test_a_redirect_to_another_public_https_address_is_followed(self):
        final = self.serve(link(host(), "/file.bin"), b"hello")
        start = self.serve(link(host()), b"", 302, {"Location": final})
        added = self.links.add(start, True)
        self.assertEqual((added["files"], added["size"]), (1, 5))
        self.assertEqual(added["downloads"][0]["sha256"], hashlib.sha256(b"hello").hexdigest())


class Archives(LinkBase):
    def bad_zip(self, data, message):
        url = self.serve(link(host(), "/a.zip"), data)
        with self.assertRaises(linksource.LinkError) as caught:
            self.links.add(url, True)
        self.assertIn(message, str(caught.exception))
        escaped = [p for p in self.tmp.rglob("*") if p.is_file() and "escape" in p.name]
        self.assertEqual(escaped, [])
        self.assertEqual(self.links.entries, [])

    def test_parent_folder_paths_are_refused(self):
        self.bad_zip(zip_bytes({"ok.txt": b"1", "../escape.txt": b"2"}), "outside")

    def test_absolute_and_drive_paths_are_refused(self):
        self.bad_zip(zip_bytes({"/escape.txt": b"2"}), "outside")
        self.bad_zip(zip_bytes({"C:/escape.txt": b"2"}), "outside")

    def test_links_inside_archives_are_refused(self):
        self.bad_zip(zip_bytes({"ok.txt": b"1"}, symlinks=["escape-link"]), "links")

    def test_highly_compressed_members_are_refused(self):
        self.bad_zip(zip_bytes({"bomb.bin": b"\0" * (8 << 20)}), "compressed")

    def test_oversize_members_are_refused(self):
        # a small download whose one member unpacks past the cap (but not so far that it counts as a bomb)
        with mock.patch.object(linksource, "PER_DOWNLOAD_CAP", 1000):
            self.bad_zip(zip_bytes({"big.bin": b"a" * 5000}), "too large")

    def test_a_good_archive_is_unpacked_into_its_own_folder(self):
        url = self.serve(link(host(), "/files.zip"), zip_bytes({"a/b.txt": b"1", "c.txt": b"2"}))
        added = self.links.add(url, True)
        self.assertEqual(added["files"], 2)
        folder = self.links.folders()[0][1]
        self.assertEqual(sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()),
                         ["files/a/b.txt", "files/c.txt"])


class Caps(LinkBase):
    def test_a_download_over_the_cap_is_refused_and_removed(self):
        url = self.serve(link(host()), os.urandom(5000))
        with mock.patch.object(linksource, "PER_DOWNLOAD_CAP", 4000):
            with self.assertRaises(linksource.LinkError) as caught:
                self.links.add(url, True)
        self.assertIn("the most Setup takes from one link", str(caught.exception))
        self.assertIn(linksource.FALLBACK, str(caught.exception))
        self.assertEqual([p for p in self.links.root.rglob("*") if p.is_file()], [])

    def test_a_lying_content_length_is_still_capped(self):
        url = self.serve(link(host()), os.urandom(5000), headers={"Content-Length": "10"})
        with mock.patch.object(linksource, "PER_DOWNLOAD_CAP", 4000), self.assertRaises(linksource.LinkError):
            self.links.add(url, True)

    def test_the_total_for_the_session_is_capped(self):
        first, second = self.serve(link(host(), "/1"), os.urandom(3000)), self.serve(link(host(), "/2"), os.urandom(3000))
        links = linksource.Links(self.opener, temp_root=self.tmp)
        self.addCleanup(links.close)
        links.budget = [5000]
        links.add(first, True)
        with self.assertRaises(linksource.LinkError) as caught:
            links.add(second, True)
        self.assertIn("space left", str(caught.exception))


class Consent(LinkBase):
    def test_no_tick_no_download(self):
        url = self.serve(link(host()), b"data")
        for consent in (False, None, "yes", 1):
            with self.subTest(consent=consent), self.assertRaises(linksource.LinkError) as caught:
                self.links.add(url, consent)
            self.assertIn(linksource.CONSENT_TEXT, str(caught.exception))
        self.assertEqual(self.web.requests, [])

    def test_the_window_needs_the_tick(self):
        state = server.State()
        state._links = self.links
        url = self.serve(link(host()), b"data")
        with self.assertRaises(ValueError):
            server.api(state, "add-link", dict(url=url))
        with self.assertRaises(ValueError):
            server.api(state, "add-link", dict(url=url, consent="true"))
        self.assertEqual(self.web.requests, [])

    def test_the_command_line_needs_the_tick(self):
        with mock.patch.object(linksource, "make_opener", lambda *a: self.opener):
            code = cli.main(["scan", "--link", self.serve(link(host()), b"data")])
        self.assertEqual(code, 2)
        self.assertEqual(self.web.requests, [])


class GitHubPages(LinkBase):
    def setUp(self):
        super().setUp()
        self.owner, self.repo = secrets.token_hex(3), secrets.token_hex(3)
        self.api = f"{linksource.GITHUB_API}/repos/{self.owner}/{self.repo}"
        self.page = urllib.parse.urlunsplit(("https", "github.com", f"/{self.owner}/{self.repo}", "", ""))

    def test_a_repository_page_becomes_its_default_branch_archive(self):
        self.serve(self.api, json.dumps(dict(default_branch="trunk")).encode())
        self.serve(f"{self.api}/zipball/trunk", zip_bytes({"x/readme.txt": b"hi"}))
        added = self.links.add(self.page, True)
        self.assertEqual(added["files"], 1)

    def test_a_release_page_becomes_its_files(self):
        assets = [link(host(), "/one.bin"), link(host(), "/two.bin")]
        for n, url in enumerate(assets):
            self.serve(url, b"x" * (n + 1))
        self.serve(f"{self.api}/releases/tags/v1", json.dumps(
            dict(assets=[dict(browser_download_url=u) for u in assets])).encode())
        added = self.links.add(self.page + "/releases/tag/v1", True)
        self.assertEqual((added["files"], added["size"]), (2, 3))

    def test_other_pages_are_downloaded_as_they_are(self):
        self.assertIsNone(linksource.resolve_github(link(host(), f"/{self.owner}/{self.repo}"), None))


class ScanAndRecords(LinkBase):
    def test_links_and_a_folder_are_scanned_together(self):
        local = fake_folder(self.tmp / "local", full=False)          # the dashboard package
        extra = fake_folder(self.tmp / "remote")                     # everything else, from a link
        shutil.rmtree(extra / "PPSA99735")
        url = self.serve(link(host(), "/mine.zip"), folder_zip(extra))
        self.links.add(url, True)
        result = catalog.scan(local, extra=self.links.folders())
        status = {i["id"]: i["status"] for i in result["items"]}
        self.assertEqual(status["dashboard-app"], "found")
        self.assertEqual(status["avatar-kit"], "found")
        kit = next(i for i in result["items"] if i["id"] == "avatar-kit")["files"][0]["name"]
        self.assertTrue(kit.startswith("from link 1/"), kit)

    def test_links_alone_can_be_scanned(self):
        url = self.serve(link(host(), "/mine.zip"), folder_zip(fake_folder(self.tmp / "remote")))
        self.links.add(url, True)
        result = catalog.scan(None, extra=self.links.folders())
        self.assertEqual(result["folder"], "")
        self.assertEqual(result["links"], ["from link 1"])

    def test_the_link_is_never_written_anywhere(self):
        secret = secrets.token_hex(8)
        h = host()
        url = self.serve(link(h, f"/{secret}/mine.zip"), folder_zip(fake_folder(self.tmp / "remote")))
        logged = []
        self.links.add(url, True, log=logged.append)
        result = catalog.scan(None, catalog.Hasher(engine.home() / "hash-cache.json"), extra=self.links.folders())
        console = targets.LocalTarget(self.tmp / "console")
        outcome = engine.Installer(console, result, dict(icons="default", ps5x360=False, companion=False),
                                   log=logged.append).run()
        self.assertTrue(outcome["ok"], outcome["summary"])
        state = server.State()
        state._links = self.links
        view = server.api(state, "scan", dict(folder=""))
        written = [json.dumps(outcome), json.dumps(view), "\n".join(logged)]
        for root in (self.tmp / "console", self.tmp / "home"):
            written += [p.read_bytes().decode("utf-8", "replace") for p in root.rglob("*") if p.is_file()]
        self.assertTrue(any("from link 1" in text for text in written))
        for text in written:
            self.assertNotIn(secret, text)
            self.assertNotIn(h, text)


class Cleanup(LinkBase):
    def test_closing_deletes_everything_downloaded(self):
        self.links.add(self.serve(link(host()), b"data"), True)
        root = self.links.root
        self.assertTrue(any(root.rglob("*")))
        self.links.close()
        self.assertFalse(root.exists())

    def test_a_failed_download_leaves_nothing(self):
        url = self.serve(link(host(), "/a.zip"), zip_bytes({"../escape.txt": b"x"}))
        with self.assertRaises(linksource.LinkError):
            self.links.add(url, True)
        self.assertEqual(list(self.links.root.iterdir()), [])

    def test_the_window_clears_links(self):
        state = server.State()
        state._links = self.links
        self.links.add(self.serve(link(host()), b"data"), True)
        root = self.links.root
        self.assertEqual(server.api(state, "clear-links", {}), dict(links=[]))
        self.assertFalse(root.exists())


class SlowBody(io.RawIOBase):
    """A response body that trickles out, so a download can be cancelled part way through."""

    def __init__(self, size):
        self.left = size

    def readable(self):
        return True

    def readinto(self, buffer):
        time.sleep(0.02)
        n = min(len(buffer), self.left, 64)
        buffer[:n] = b"s" * n
        self.left -= n
        return n


class DownloadZipRoutes(LinkBase):
    """GitHub's Download ZIP: the repository page, a branch or tag page, and the button's own link."""

    def setUp(self):
        super().setUp()
        self.owner, self.repo = secrets.token_hex(3), secrets.token_hex(3)
        self.api = f"{linksource.GITHUB_API}/repos/{self.owner}/{self.repo}"
        self.page = urllib.parse.urlunsplit(("https", "github.com", f"/{self.owner}/{self.repo}", "", ""))

    def test_a_branch_page_becomes_that_branch_archive(self):
        branch = "feature/" + secrets.token_hex(2)
        self.serve(f"{self.api}/zipball/{branch}", zip_bytes({"x/one.txt": b"1"}))
        self.assertEqual(linksource.resolve_github(f"{self.page}/tree/{branch}", None),
                         [f"{self.api}/zipball/{branch}"])
        self.assertEqual(self.links.add(f"{self.page}/tree/{branch}", True)["files"], 1)

    def test_the_download_zip_button_link_is_used_as_it_is(self):
        button = f"{self.page}/archive/refs/heads/{secrets.token_hex(2)}.zip"
        self.assertIsNone(linksource.resolve_github(button, None))
        served = self.serve(link(host(), "/archive.zip"), zip_bytes({"x/a.txt": b"1", "x/b.txt": b"2"}))
        self.serve(button, b"", 302, {"Location": served})  # the archive is handed over from another host
        self.assertEqual(self.links.add(button, True)["files"], 2)

    def test_the_repository_page_still_resolves(self):
        self.serve(self.api, json.dumps(dict(default_branch="main")).encode())
        self.serve(f"{self.api}/zipball/main", zip_bytes({"x/a.txt": b"1"}))
        self.assertEqual(self.links.add(self.page, True)["files"], 1)


class PlainMessages(LinkBase):
    def test_sign_in_needed(self):
        for code in (401, 403, 404):
            url = self.serve(link(host()), b"", code)
            with self.subTest(code=code), self.assertRaises(linksource.LinkError) as caught:
                self.links.add(url, True)
            self.assertEqual(str(caught.exception), linksource.SIGN_IN)
        self.assertIn("browser", linksource.SIGN_IN)
        self.assertIn("never asks for passwords or tokens", linksource.SIGN_IN)

    def test_a_private_repository_page_says_sign_in(self):
        owner, repo = secrets.token_hex(3), secrets.token_hex(3)
        self.serve(f"{linksource.GITHUB_API}/repos/{owner}/{repo}", b"{}", 404)
        page = urllib.parse.urlunsplit(("https", "github.com", f"/{owner}/{repo}", "", ""))
        with self.assertRaises(linksource.LinkError) as caught:
            self.links.add(page, True)
        self.assertEqual(str(caught.exception), linksource.SIGN_IN)

    def test_no_credentials_are_ever_sent(self):
        seen = []
        real = self.web.https_open
        self.web.https_open = lambda req: (seen.append(dict(req.header_items())), real(req))[1]
        self.links.add(self.serve(link(host()), b"x"), True)
        self.assertTrue(seen)
        self.assertFalse(any(k.lower() in ("authorization", "cookie") for h in seen for k in h))

    def test_timeouts_say_so_and_offer_the_browser(self):
        def slow(req):
            raise urllib.error.URLError(TimeoutError("timed out"))
        self.web.https_open = slow
        with self.assertRaises(linksource.LinkError) as caught:
            self.links.add(link(host()), True)
        self.assertIn("timed out", str(caught.exception))
        self.assertIn(linksource.FALLBACK, str(caught.exception))

    def test_git_lfs_placeholders_are_named(self):
        pointer = linksource.LFS_HEAD + b"v1\noid sha256:" + b"0" * 64 + b"\nsize 123456789\n"
        url = self.serve(link(host(), "/repo.zip"), zip_bytes({"repo/avatareditor.xex": pointer}))
        added = self.links.add(url, True)
        self.assertEqual(added["warnings"], [linksource.LFS_TEXT])
        result = catalog.scan(None, extra=self.links.folders())
        self.assertTrue(any("placeholder" in line and "Git LFS" in line for line in result["wrong"]))
        self.assertNotEqual(next(i for i in result["items"] if i["id"] == "editor")["status"], "found")


class ProgressAndCancel(LinkBase):
    def test_progress_reports_size_so_far_and_total(self):
        url = self.serve(link(host()), os.urandom(5000))
        seen = []
        with mock.patch.object(linksource, "CHUNK", 1000):
            self.links.add(url, True, progress=lambda step, done, total: seen.append((done, total)))
        self.assertEqual(seen[0], (0, 5000))
        self.assertEqual(seen[-1], (5000, 5000))

    def test_cancel_removes_the_partial_file(self):
        cancel = threading.Event()
        url = self.serve(link(host()), os.urandom(5000))

        def stop_after_first(step, done, total):
            if done:
                cancel.set()
        with mock.patch.object(linksource, "CHUNK", 1000), self.assertRaises(linksource.Cancelled):
            self.links.add(url, True, progress=stop_after_first, cancel=cancel)
        self.assertEqual(list(self.links.root.rglob("*")), [])
        self.assertEqual(self.links.entries, [])

    def test_the_window_shows_progress_and_cancels(self):
        state = server.State()
        state._links = self.links
        url = link(host())
        real = self.web.https_open

        def slow(req):
            if req.full_url != url:
                return real(req)
            message = email.message.Message()
            message["Content-Length"] = "100000"
            response = urllib.response.addinfourl(io.BufferedReader(SlowBody(100000)), message, url, 200)
            response.msg = "slow"
            return response
        self.web.https_open = slow
        small_reads = mock.patch.object(linksource, "CHUNK", 64)  # the slow body hands out 64 bytes at a time
        small_reads.start()
        self.addCleanup(small_reads.stop)
        self.assertEqual(server.api(state, "add-link", dict(url=url, consent=True)), dict(started=True))
        view = {}
        for _ in range(200):
            view = server.api(state, "link-progress", {})
            if view.get("done"):
                break
            time.sleep(0.02)
        self.assertGreater(view["done"], 0, view)
        self.assertEqual(view["total"], 100000)
        server.api(state, "cancel-link", {})
        state.link_job.thread.join(10)
        view = server.api(state, "link-progress", {})
        self.assertFalse(view["running"])
        self.assertIn("Cancelled", view["error"])
        self.assertEqual(view["links"], [])
        self.assertEqual([p for p in self.links.root.rglob("*") if p.is_file()], [])


class ZipsOnThisPC(LinkBase):
    """A ZIP already downloaded works with no link: in the folder, or picked directly."""

    def kit_zip(self, where: Path, name="my-files.zip", extra=None) -> Path:
        files = {"stuff/kit360.nxak": b"NXAK\x02\x00\x00\x00 fake kit", **(extra or {})}
        where.mkdir(parents=True, exist_ok=True)
        path = where / name
        path.write_bytes(zip_bytes(files))
        return path

    def test_a_zip_in_the_folder_is_looked_inside(self):
        folder = self.tmp / "folder"
        self.kit_zip(folder)
        result = catalog.scan_all(folder, self.links)
        kit = next(i for i in result["items"] if i["id"] == "avatar-kit")
        self.assertEqual(kit["status"], "found")
        self.assertTrue(kit["files"][0]["name"].startswith("inside my-files.zip/"), kit["files"][0]["name"])
        self.assertEqual(self.links.summary(), [])  # not counted as a link

    def test_a_zip_can_be_picked_directly(self):
        archive = self.kit_zip(self.tmp / "downloads")
        result = catalog.scan_all(archive, self.links)
        self.assertEqual(next(i for i in result["items"] if i["id"] == "avatar-kit")["status"], "found")

    def test_an_unsafe_zip_in_the_folder_is_reported_and_skipped(self):
        folder = self.tmp / "folder"
        folder.mkdir()
        (folder / "bad.zip").write_bytes(zip_bytes({"../escape.txt": b"x"}))
        self.kit_zip(folder)
        result = catalog.scan_all(folder, self.links)
        self.assertTrue(any(line.startswith("bad.zip:") and "outside" in line for line in result["wrong"]))
        self.assertEqual(next(i for i in result["items"] if i["id"] == "avatar-kit")["status"], "found")
        self.assertEqual(list(self.tmp.rglob("escape.txt")), [])

    def test_zips_inside_zips_are_not_unpacked_again(self):
        inner = zip_bytes({"kit360.nxak": b"NXAK\x02\x00\x00\x00 fake kit"})
        self.kit_zip(self.tmp / "folder", extra={"nested/inner.zip": inner})
        catalog.scan_all(self.tmp / "folder", self.links)
        self.assertEqual(len(self.links.entries), 1)

    def test_a_rescan_does_not_unpack_twice(self):
        folder = self.tmp / "folder"
        self.kit_zip(folder)
        catalog.scan_all(folder, self.links)
        catalog.scan_all(folder, self.links)
        self.assertEqual(len(self.links.entries), 1)


if __name__ == "__main__":
    unittest.main()
