"""Add files from a link: a convenience for using your own files from a link (for example your own
cloud storage) instead of a folder on this PC.

The link is the user's choice; Setup never suggests one. What a link delivers is downloaded into a
temporary folder and scanned exactly like a local folder, so only files that pass the normal checks
are used; everything else is deleted with the temporary folder.

Rules this module enforces:
- https only; no addresses on this PC or the local network, checked again after every redirect.
- The user must confirm "I have the right to use these files" before anything is downloaded.
- Size caps per download and in total, timeouts, and safe ZIP extraction (no absolute paths, no
  "..", no links, no oversize or highly compressed members).
- Nothing downloaded is run, imported or opened beyond reading ZIP entries and hashing.
- The link itself is never kept: not in records, logs, settings or error messages. Only
  "from a link", sizes and SHA-256 fingerprints are reported.
"""
from __future__ import annotations

import hashlib
import ipaddress
import json
import posixpath
import re
import shutil
import socket
import stat
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

CONSENT_TEXT = "I have the right to use these files."
PER_DOWNLOAD_CAP = 8 << 30      # 8 GiB for any one download
TOTAL_CAP = 32 << 30            # 32 GiB for all links in one session
TIMEOUT = 60                    # seconds without data before a download is abandoned
MAX_REDIRECTS = 10
MAX_MEMBERS = 50_000
MAX_RATIO = 200                 # a ZIP member may expand at most 200x (when larger than 1 MiB)
CHUNK = 1 << 20
GITHUB_API = "https://api.github.com"


class LinkError(ValueError):
    """A problem with a link. Messages never contain the link itself."""


# ---------- which links are allowed ----------
def _bad_address(address: str) -> bool:
    ip = ipaddress.ip_address(address)
    if ip.version == 6 and ip.ipv4_mapped:
        ip = ip.ipv4_mapped
    return not ip.is_global or ip.is_multicast or ip.is_reserved or ip.is_unspecified


def check_url(url: str, resolve=socket.getaddrinfo) -> urllib.parse.SplitResult:
    """Accept only an https link to a public internet address. Raises LinkError otherwise."""
    if not isinstance(url, str) or not url.strip() or len(url) > 4096 or any(c.isspace() for c in url.strip()):
        raise LinkError("That is not a link Setup can use.")
    parts = urllib.parse.urlsplit(url.strip())
    if parts.scheme.lower() != "https":
        raise LinkError("Only https links can be used.")
    host = parts.hostname
    if not host:
        raise LinkError("That link has no address.")
    if parts.username or parts.password:
        raise LinkError("Links with a name or password in them cannot be used.")
    try:
        port = parts.port or 443
    except ValueError:
        raise LinkError("That link has an invalid port.") from None
    if host.lower() in ("localhost",) or host.lower().endswith((".localhost", ".local", ".internal", ".lan",
                                                               ".home", ".arpa")):
        raise LinkError("Links to this PC or your local network cannot be used.")
    try:
        literal = ipaddress.ip_address(host)
    except ValueError:
        literal = None
    if literal is not None:
        if _bad_address(str(literal)):
            raise LinkError("Links to this PC or your local network cannot be used.")
        return parts
    try:
        answers = resolve(host, port, proto=socket.IPPROTO_TCP)
    except (OSError, UnicodeError):
        raise LinkError("That link's address could not be found.") from None
    addresses = {a[4][0].split("%")[0] for a in answers}
    if not addresses or any(_bad_address(a) for a in addresses):
        raise LinkError("Links to this PC or your local network cannot be used.")
    return parts


class _CheckedRedirects(urllib.request.HTTPRedirectHandler):
    """Every redirect target goes through check_url again (https, public address)."""
    max_redirections = MAX_REDIRECTS

    def __init__(self, resolve):
        super().__init__()
        self.resolve = resolve

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        try:
            check_url(urllib.parse.urljoin(req.full_url, newurl), self.resolve)
        except LinkError:
            fp.close()
            raise
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None:  # never carry credentials to another place
            new.remove_header("Authorization")
            new.remove_header("Cookie")
        return new


def make_opener(resolve=socket.getaddrinfo, *extra_handlers):
    # Only https is installed: no http, ftp, file or data handlers, so nothing else can be opened.
    opener = urllib.request.OpenerDirector()
    for handler in (urllib.request.HTTPSHandler(), urllib.request.HTTPDefaultErrorHandler(),
                    urllib.request.HTTPErrorProcessor(), _CheckedRedirects(resolve), *extra_handlers):
        opener.add_handler(handler)
    opener.addheaders = [("User-Agent", "ExBox360-Setup"), ("Accept", "*/*")]
    opener.resolve = resolve  # the same address check for the first link and every redirect
    return opener


# ---------- GitHub repository, branch and release pages ----------
_GH = re.compile(r"^/([A-Za-z0-9._-]+)/([A-Za-z0-9._-]+?)(?:\.git)?(/.*)?$")


def resolve_github(url: str, read_json) -> list[str] | None:
    """A github.com repository, branch/tag or release page -> the file links it stands for.
    Repository page: its default-branch archive. Branch or tag page (/tree/<name>): that archive.
    Release page: that release's files. Anything else (including the link behind the Download ZIP
    button) is not a page and is downloaded as it is (None)."""
    parts = urllib.parse.urlsplit(url)
    if (parts.hostname or "").lower() not in ("github.com", "www.github.com"):
        return None
    match = _GH.match(parts.path.rstrip("/"))
    if not match:
        return None
    owner, repo, rest = match.group(1), match.group(2), match.group(3) or ""
    api = f"{GITHUB_API}/repos/{owner}/{repo}"
    if rest == "":
        branch = read_json(api).get("default_branch")
        if not branch:
            raise LinkError("That repository's default branch could not be found.")
        return [f"{api}/zipball/{urllib.parse.quote(branch, safe='/')}"]
    if rest.startswith("/tree/") and len(rest) > len("/tree/"):
        ref = urllib.parse.unquote(rest[len("/tree/"):])
        return [f"{api}/zipball/{urllib.parse.quote(ref, safe='/')}"]
    release = re.fullmatch(r"/releases(?:/tag/([^/]+)|/latest)?", rest)
    if release:
        if rest in ("/releases", "/releases/latest"):
            data = read_json(f"{api}/releases/latest")
        else:
            data = read_json(f"{api}/releases/tags/{urllib.parse.quote(urllib.parse.unquote(release.group(1)))}")
        assets = [a.get("browser_download_url") for a in data.get("assets", []) if a.get("browser_download_url")]
        if not assets:
            raise LinkError("That release has no files attached.")
        return assets
    return None


# ---------- plain-English messages (none of them repeat the link) ----------
FALLBACK = "Download it in your browser instead, then choose the downloaded file or folder in Setup."
SIGN_IN = ("That link needs a sign-in, or is private or no longer there. " + FALLBACK
           + " Setup never asks for passwords or tokens.")
LFS_TEXT = ("This download contains placeholders, not the real files (it was stored with Git LFS). "
            "Download the files themselves and point Setup at them.")
LFS_HEAD = b"version https://git-lfs.github.com/spec/"


class Cancelled(LinkError):
    pass


def _http_error(error: urllib.error.HTTPError) -> LinkError:
    error.close()
    if error.code in (401, 403, 404, 407, 451):
        return LinkError(SIGN_IN)
    return LinkError(f"The link answered with an error ({error.code}). " + FALLBACK)


def _timed_out() -> LinkError:
    return LinkError(f"The download timed out (no data for {TIMEOUT} seconds). " + FALLBACK)


def _too_big(limit: int, budget: list[int]) -> str:
    if limit < PER_DOWNLOAD_CAP:
        return (f"That is larger than the space left for links in this session (the {TOTAL_CAP >> 30} GB "
                f"session limit; {max(limit, 0) / (1 << 30):.1f} GB left). " + FALLBACK)
    return f"That is larger than {PER_DOWNLOAD_CAP >> 30} GB, the most Setup takes from one link. " + FALLBACK


def lfs_placeholders(folder: Path) -> int:
    """How many Git LFS pointer files (small text stand-ins for the real files) a folder holds."""
    count = 0
    for path in folder.rglob("*"):
        try:
            if path.is_file() and not path.is_symlink() and path.stat().st_size < 1024:
                with path.open("rb") as f:
                    count += f.read(len(LFS_HEAD)) == LFS_HEAD
        except OSError:
            continue
    return count


# ---------- downloading ----------
def _file_name(response, url: str, index: int) -> str:
    disposition = response.headers.get("Content-Disposition", "")
    match = re.search(r"filename\*?=(?:UTF-8'')?\"?([^\";]+)", disposition)
    name = urllib.parse.unquote(match.group(1)) if match else posixpath.basename(urllib.parse.urlsplit(url).path)
    name = re.sub(r"[^A-Za-z0-9._ -]", "_", name).strip(" .")[:120]
    return name or f"download-{index}"


def download(url: str, folder: Path, budget: list[int], opener, index: int = 1, progress=None,
             cancel=None) -> tuple[Path, int, str]:
    """Stream one link into folder. budget[0] is what is left of the session's total cap.
    progress(done, total_or_None) is called as data arrives; cancel.is_set() stops the download and
    removes the partial file. Returns (file, size, sha256)."""
    resolve = getattr(opener, "resolve", socket.getaddrinfo)
    check_url(url, resolve)
    try:
        response = opener.open(urllib.request.Request(url), timeout=TIMEOUT)
    except urllib.error.HTTPError as error:
        raise _http_error(error) from None
    except LinkError:
        raise
    except TimeoutError:
        raise _timed_out() from None
    except (urllib.error.URLError, OSError, ValueError) as error:
        if isinstance(getattr(error, "reason", None), TimeoutError):
            raise _timed_out() from None
        raise LinkError("The link could not be reached. " + FALLBACK) from None
    with response:
        check_url(response.geturl(), resolve)
        declared = response.headers.get("Content-Length")
        total = int(declared) if declared and declared.isdigit() else None
        limit = min(PER_DOWNLOAD_CAP, budget[0])
        if total is not None and total > limit:
            raise LinkError(_too_big(limit, budget))
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / _file_name(response, response.geturl(), index)
        digest, size = hashlib.sha256(), 0
        try:
            with path.open("xb") as out:
                if progress:
                    progress(0, total)
                while True:
                    if cancel is not None and cancel.is_set():
                        raise Cancelled("Cancelled. Nothing from that link was kept.")
                    try:
                        chunk = response.read(CHUNK)
                    except TimeoutError:
                        raise _timed_out() from None
                    except (OSError, ValueError):
                        raise LinkError("The download stopped before it finished. " + FALLBACK) from None
                    if not chunk:
                        break
                    size += len(chunk)
                    if size > limit:
                        raise LinkError(_too_big(limit, budget))
                    digest.update(chunk)
                    out.write(chunk)
                    if progress:
                        progress(size, total)
        except BaseException:
            path.unlink(missing_ok=True)
            raise
    budget[0] -= size
    return path, size, digest.hexdigest()


# ---------- safe ZIP extraction ----------
def safe_extract(archive: Path, dest: Path, budget: list[int], cancel=None) -> int:
    """Unpack a ZIP without trusting it (downloaded or already on this PC). Returns files written."""
    try:
        zf = zipfile.ZipFile(archive)
    except (zipfile.BadZipFile, OSError):
        raise LinkError("A ZIP file is damaged, so it was not unpacked.") from None
    with zf:
        members = zf.infolist()
        if len(members) > MAX_MEMBERS:
            raise LinkError("A ZIP file holds too many files to unpack.")
        planned = 0
        for m in members:
            name = m.filename.replace("\\", "/")
            parts = [p for p in name.split("/") if p not in ("", ".")]
            if name.startswith("/") or re.match(r"^[A-Za-z]:", name) or ".." in parts:
                raise LinkError("A ZIP file tries to write outside its own folder, so it was not unpacked.")
            kind = (m.external_attr >> 16) & 0o170000
            if kind == stat.S_IFLNK or (kind and kind not in (stat.S_IFREG, stat.S_IFDIR)):
                raise LinkError("A ZIP file contains links, which Setup does not unpack.")
            if m.file_size > PER_DOWNLOAD_CAP:
                raise LinkError(f"A ZIP file holds a file that is too large (over {PER_DOWNLOAD_CAP >> 30} GB) "
                                "to unpack. Unpack it yourself and choose the folder instead.")
            if m.file_size > (1 << 20) and m.file_size > MAX_RATIO * max(m.compress_size, 1):
                raise LinkError("A ZIP file is compressed suspiciously far, so it was not unpacked.")
            planned += m.file_size
        if planned > budget[0]:
            raise LinkError(f"A ZIP file unpacks to more than the space left this session (the "
                            f"{TOTAL_CAP >> 30} GB limit). Unpack it yourself and choose the folder instead.")
        written = 0
        root = dest.resolve()
        for m in members:
            if cancel is not None and cancel.is_set():
                raise Cancelled("Cancelled. Nothing from it was kept.")
            parts = [p for p in m.filename.replace("\\", "/").split("/") if p not in ("", ".")]
            if not parts:
                continue
            target = root.joinpath(*parts)
            if not target.resolve().is_relative_to(root):
                raise LinkError("A ZIP file tries to write outside its own folder, so it was not unpacked.")
            if m.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            size = 0
            with zf.open(m) as src, target.open("xb") as out:
                while True:
                    chunk = src.read(CHUNK)
                    if not chunk:
                        break
                    size += len(chunk)
                    if size > m.file_size:  # the header lied about the size
                        raise LinkError("A ZIP file is damaged, so it was not unpacked.")
                    out.write(chunk)
            written += 1
        budget[0] -= planned
        return written


# ---------- one Setup session's links and ZIP files ----------
class Links:
    """Files added in one Setup session from links or from ZIP files on this PC, each unpacked into its
    own temporary folder. Keeps folders, sizes and fingerprints; never the links."""

    def __init__(self, opener=None, temp_root: Path | None = None):
        self.opener = opener or make_opener()
        self.root = Path(tempfile.mkdtemp(prefix="exbox-links-", dir=temp_root))
        self.budget = [TOTAL_CAP]
        self.entries: list[dict] = []
        self._zips: dict[tuple, dict] = {}  # a ZIP on this PC is unpacked once per session

    def folders(self) -> list[tuple[str, Path]]:
        return [(e["label"], Path(e["folder"])) for e in self.entries]

    def summary(self) -> list[dict]:
        return [dict(label=e["label"], files=e["files"], size=e["size"], warnings=e.get("warnings", []))
                for e in self.entries if e["kind"] == "link"]

    def _read_json(self, url: str) -> dict:
        try:
            with self.opener.open(urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"}),
                                  timeout=TIMEOUT) as response:
                return json.loads(response.read(4 << 20))
        except urllib.error.HTTPError as error:
            raise _http_error(error) from None
        except TimeoutError:
            raise _timed_out() from None
        except (urllib.error.URLError, OSError, ValueError):
            raise LinkError("GitHub could not be reached for that page. " + FALLBACK) from None

    def _warnings(self, folder: Path) -> list[str]:
        return [LFS_TEXT] if lfs_placeholders(folder) else []

    def add(self, url: str, consent: bool, log=None, progress=None, cancel=None) -> dict:
        """Download one link into its own folder. Returns its summary (no link in it).
        progress(text, done, total_or_None) reports bytes as they arrive."""
        if consent is not True:
            raise LinkError("Tick \"" + CONSENT_TEXT + "\" first. Nothing was downloaded.")
        check_url(url, getattr(self.opener, "resolve", socket.getaddrinfo))
        index = sum(e["kind"] == "link" for e in self.entries) + 1
        label = f"from link {index}"
        folder = self.root / f"link-{index}"
        try:
            targets = resolve_github(url.strip(), self._read_json) or [url.strip()]
            files, total, prints = 0, 0, []
            for n, target in enumerate(targets, 1):
                step = f"file {n} of {len(targets)}" if len(targets) > 1 else "download"
                report = (lambda done, size, step=step: progress(step, done, size)) if progress else None
                path, size, sha = download(target, folder / "downloads", self.budget, self.opener, n, report, cancel)
                total += size
                prints.append(dict(size=size, sha256=sha))
                if log:
                    log(f"Downloaded a file from a link: {size:,} bytes, sha256 {sha}")
                if zipfile.is_zipfile(path):
                    if progress:
                        progress("unpacking", size, size)
                    files += safe_extract(path, folder / path.stem, self.budget, cancel)
                    path.unlink()
                else:
                    files += 1
            warnings = self._warnings(folder)
        except BaseException:
            shutil.rmtree(folder, ignore_errors=True)
            raise
        entry = dict(kind="link", label=label, folder=str(folder), files=files, size=total, downloads=prints,
                     warnings=warnings)
        self.entries.append(entry)
        return dict(label=label, files=files, size=total, downloads=prints, warnings=warnings)

    def add_zip(self, archive: Path, cancel=None) -> dict:
        """Unpack a ZIP file already on this PC (no download, no link) with the same safe extraction."""
        archive = Path(archive).resolve()
        info = archive.stat()
        key = (str(archive), info.st_size, info.st_mtime_ns)
        if key in self._zips:
            return self._zips[key]
        index = sum(e["kind"] == "zip" for e in self.entries) + 1
        folder = self.root / f"zip-{index}"
        try:
            files = safe_extract(archive, folder, self.budget, cancel)
            warnings = self._warnings(folder)
        except BaseException:
            shutil.rmtree(folder, ignore_errors=True)
            raise
        entry = dict(kind="zip", label=f"inside {archive.name}", folder=str(folder), files=files,
                     size=info.st_size, warnings=warnings)
        self.entries.append(entry)
        self._zips[key] = entry
        return entry

    def close(self):
        """Delete everything downloaded or unpacked in this session."""
        shutil.rmtree(self.root, ignore_errors=True)
        self.entries.clear()
        self._zips.clear()
