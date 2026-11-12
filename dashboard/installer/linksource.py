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


# ---------- GitHub repository and release pages ----------
_GH = re.compile(r"^/([A-Za-z0-9._-]+)/([A-Za-z0-9._-]+?)(?:\.git)?(/.*)?$")


def resolve_github(url: str, read_json) -> list[str] | None:
    """A github.com repository or release page -> the file links it stands for (None if it is neither).
    Repository: its default-branch archive. Release: that release's assets."""
    parts = urllib.parse.urlsplit(url)
    if (parts.hostname or "").lower() not in ("github.com", "www.github.com"):
        return None
    match = _GH.match(parts.path.rstrip("/"))
    if not match:
        return None
    owner, repo, rest = match.group(1), match.group(2), match.group(3) or ""
    api = f"{GITHUB_API}/repos/{owner}/{repo}"
    if rest in ("",):
        branch = read_json(api).get("default_branch")
        if not branch:
            raise LinkError("That repository's default branch could not be found.")
        return [f"{api}/zipball/{urllib.parse.quote(branch)}"]
    release = re.fullmatch(r"/releases(?:/tag/([^/]+)|/latest)?", rest)
    if release:
        if rest == "/releases" or rest == "/releases/latest":
            data = read_json(f"{api}/releases/latest")
        else:
            data = read_json(f"{api}/releases/tags/{urllib.parse.quote(urllib.parse.unquote(release.group(1)))}")
        assets = [a.get("browser_download_url") for a in data.get("assets", []) if a.get("browser_download_url")]
        if not assets:
            raise LinkError("That release has no files attached.")
        return assets
    return None  # any other github.com page or file link is downloaded as it is


# ---------- downloading ----------
def _file_name(response, url: str, index: int) -> str:
    disposition = response.headers.get("Content-Disposition", "")
    match = re.search(r"filename\*?=(?:UTF-8'')?\"?([^\";]+)", disposition)
    name = urllib.parse.unquote(match.group(1)) if match else posixpath.basename(urllib.parse.urlsplit(url).path)
    name = re.sub(r"[^A-Za-z0-9._ -]", "_", name).strip(" .")[:120]
    return name or f"download-{index}"


def download(url: str, folder: Path, budget: list[int], opener, index: int = 1) -> tuple[Path, int, str]:
    """Stream one link into folder. budget[0] is what is left of the session's total cap.
    Returns (file, size, sha256)."""
    check_url(url, getattr(opener, "resolve", socket.getaddrinfo))
    try:
        response = opener.open(urllib.request.Request(url), timeout=TIMEOUT)
    except urllib.error.HTTPError as error:
        error.close()
        raise LinkError(f"The link answered with an error ({error.code}).") from None
    except LinkError:
        raise
    except (urllib.error.URLError, OSError, ValueError):
        raise LinkError("The link could not be reached.") from None
    with response:
        final = response.geturl()
        check_url(final, getattr(opener, "resolve", socket.getaddrinfo))
        declared = response.headers.get("Content-Length")
        limit = min(PER_DOWNLOAD_CAP, budget[0])
        if declared and declared.isdigit() and int(declared) > limit:
            raise LinkError(_too_big(limit, budget))
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / _file_name(response, final, index)
        digest, size = hashlib.sha256(), 0
        try:
            with path.open("xb") as out:
                while True:
                    try:
                        chunk = response.read(CHUNK)
                    except (OSError, ValueError):
                        raise LinkError("The download stopped before it finished.") from None
                    if not chunk:
                        break
                    size += len(chunk)
                    if size > limit:
                        raise LinkError(_too_big(limit, budget))
                    digest.update(chunk)
                    out.write(chunk)
        except BaseException:
            path.unlink(missing_ok=True)
            raise
    budget[0] -= size
    return path, size, digest.hexdigest()


def _too_big(limit: int, budget: list[int]) -> str:
    if limit < PER_DOWNLOAD_CAP:
        return (f"That download is larger than the space left for links in this session "
                f"({limit / (1 << 30):.1f} GB of {TOTAL_CAP >> 30} GB). Use a folder on this PC for large files.")
    return (f"That download is larger than {PER_DOWNLOAD_CAP >> 30} GB, the most Setup takes from one link. "
            "Use a folder on this PC for large files.")


# ---------- safe ZIP extraction ----------
def safe_extract(archive: Path, dest: Path, budget: list[int]) -> int:
    """Unpack a ZIP without trusting it. Returns the number of files written."""
    try:
        zf = zipfile.ZipFile(archive)
    except (zipfile.BadZipFile, OSError):
        raise LinkError("A downloaded ZIP file is damaged.") from None
    with zf:
        members = zf.infolist()
        if len(members) > MAX_MEMBERS:
            raise LinkError("A downloaded ZIP file holds too many files.")
        planned = 0
        for m in members:
            name = m.filename.replace("\\", "/")
            parts = [p for p in name.split("/") if p not in ("", ".")]
            if name.startswith("/") or re.match(r"^[A-Za-z]:", name) or ".." in parts:
                raise LinkError("A downloaded ZIP file tries to write outside its own folder.")
            kind = (m.external_attr >> 16) & 0o170000
            if kind == stat.S_IFLNK or (kind and kind not in (stat.S_IFREG, stat.S_IFDIR)):
                raise LinkError("A downloaded ZIP file contains links, which Setup does not unpack.")
            if m.file_size > PER_DOWNLOAD_CAP:
                raise LinkError("A downloaded ZIP file holds a file that is too large.")
            if m.file_size > (1 << 20) and m.file_size > MAX_RATIO * max(m.compress_size, 1):
                raise LinkError("A downloaded ZIP file is compressed suspiciously far, so it was not unpacked.")
            planned += m.file_size
        if planned > budget[0]:
            raise LinkError(_too_big(budget[0], budget))
        written = 0
        root = dest.resolve()
        for m in members:
            parts = [p for p in m.filename.replace("\\", "/").split("/") if p not in ("", ".")]
            if not parts:
                continue
            target = root.joinpath(*parts)
            if not target.resolve().is_relative_to(root):
                raise LinkError("A downloaded ZIP file tries to write outside its own folder.")
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
                        raise LinkError("A downloaded ZIP file is damaged.")
                    out.write(chunk)
            written += 1
        budget[0] -= planned
        return written


# ---------- one Setup session's links ----------
class Links:
    """The links added in one Setup session. Keeps folders, sizes and fingerprints; never the links."""

    def __init__(self, opener=None, temp_root: Path | None = None):
        self.opener = opener or make_opener()
        self.root = Path(tempfile.mkdtemp(prefix="exbox-links-", dir=temp_root))
        self.budget = [TOTAL_CAP]
        self.entries: list[dict] = []

    def folders(self) -> list[tuple[str, Path]]:
        return [(e["label"], Path(e["folder"])) for e in self.entries]

    def summary(self) -> list[dict]:
        return [dict(label=e["label"], files=e["files"], size=e["size"]) for e in self.entries]

    def _read_json(self, url: str) -> dict:
        try:
            with self.opener.open(urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"}),
                                  timeout=TIMEOUT) as response:
                return json.loads(response.read(4 << 20))
        except urllib.error.HTTPError as error:
            error.close()
            raise LinkError(f"GitHub answered with an error ({error.code}) for that page.") from None
        except (urllib.error.URLError, OSError, ValueError):
            raise LinkError("GitHub could not be reached for that page.") from None

    def add(self, url: str, consent: bool, log=None) -> dict:
        """Download one link into its own folder. Returns its summary (no link in it)."""
        if consent is not True:
            raise LinkError("Tick \"" + CONSENT_TEXT + "\" first. Nothing was downloaded.")
        check_url(url, getattr(self.opener, "resolve", socket.getaddrinfo))
        index = len(self.entries) + 1
        label = f"from link {index}"
        folder = self.root / f"link-{index}"
        try:
            targets = resolve_github(url.strip(), self._read_json) or [url.strip()]
            files, total, prints = 0, 0, []
            for n, target in enumerate(targets, 1):
                path, size, sha = download(target, folder / "downloads", self.budget, self.opener, n)
                total += size
                prints.append(dict(size=size, sha256=sha))
                if log:
                    log(f"Downloaded a file from a link: {size:,} bytes, sha256 {sha}")
                if zipfile.is_zipfile(path):
                    files += safe_extract(path, folder / path.stem, self.budget)
                    path.unlink()
                else:
                    files += 1
        except BaseException:
            shutil.rmtree(folder, ignore_errors=True)
            raise
        entry = dict(label=label, folder=str(folder), files=files, size=total, downloads=prints)
        self.entries.append(entry)
        return dict(label=label, files=files, size=total, downloads=prints)

    def close(self):
        """Delete everything downloaded in this session."""
        shutil.rmtree(self.root, ignore_errors=True)
        self.entries.clear()
