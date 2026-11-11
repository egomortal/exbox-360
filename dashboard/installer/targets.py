"""Where files get installed: a real PS5 over FTP, or a practice folder on this PC laid out like one.

Both speak the same small interface, so the whole install can be rehearsed with no console.
Every write is checked against an allow-list first: only our own titles, /data/nxe, our payload
folder, the one autoload file, and the name/icon files the branding step owns.
"""
from __future__ import annotations

import hashlib
import io
import shutil
from ftplib import FTP, error_perm, all_errors
from pathlib import Path, PurePosixPath

OWNED = (
    "/data/homebrew/PPSA99735/", "/data/homebrew/PPSA99734/", "/data/homebrew/PPSA50011/",
    "/data/homebrew/ExBox360/", "/data/nxe/", "/data/pldmgr/payloads/ExBox360-Daemon/",
    "/user/appmeta/PPSA99735/", "/user/appmeta/PPSA99734/",
)
OWNED_FILES = ("/data/pldmgr/autoload.txt", "/system_data/priv/mms/app.db")
MISSING_SIZE = (1 << 64) - 1  # ftpsrv answers this for a file that is not there


class NotAllowed(Exception):
    pass


def clean(path: str) -> str:
    pure = PurePosixPath(path)
    if not path.startswith("/") or ".." in pure.parts or "\\" in path or "\x00" in path:
        raise NotAllowed(f"Unsafe console path: {path}")
    return str(pure)


def check_write(path: str) -> str:
    path = clean(path)
    if any(path.startswith(prefix) for prefix in OWNED):
        return path
    if any(path == name or path.startswith(name + ".") for name in OWNED_FILES):
        return path
    raise NotAllowed(f"The installer is not allowed to change {path}")


class Target:
    """Shared behaviour. Paths are console paths such as /data/nxe/x.json."""
    practice = False
    label = ""

    def read(self, path: str, limit: int = 64 << 20) -> bytes | None:
        raise NotImplementedError

    def sha256(self, path: str) -> str | None:
        digest = hashlib.sha256()
        if not self.stream(path, digest.update):
            return None
        return digest.hexdigest()

    def write(self, path: str, data: bytes) -> None:
        self.upload(path, io.BytesIO(data))

    def close(self) -> None:
        pass

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class LocalTarget(Target):
    """A folder on this PC that stands in for the console (practice runs and tests)."""
    practice = True

    def __init__(self, root: Path):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.label = f"practice folder {self.root}"

    def _local(self, path: str) -> Path:
        return self.root / clean(path).lstrip("/")

    def size(self, path):
        local = self._local(path)
        return local.stat().st_size if local.is_file() else None

    def is_dir(self, path):
        return self._local(path).is_dir()

    def read(self, path, limit=64 << 20):
        local = self._local(path)
        if not local.is_file():
            return None
        if local.stat().st_size > limit:
            raise ValueError(f"{path} is larger than expected")
        return local.read_bytes()

    def stream(self, path, sink):
        local = self._local(path)
        if not local.is_file():
            return False
        with local.open("rb") as source:
            for block in iter(lambda: source.read(1 << 20), b""):
                sink(block)
        return True

    def upload(self, path, source, progress=None):
        local = self._local(check_write(path))
        local.parent.mkdir(parents=True, exist_ok=True)
        with local.open("wb") as out:
            for block in iter(lambda: source.read(1 << 20), b""):
                out.write(block)
                if progress:
                    progress(len(block))

    def rename(self, old, new):
        check_write(old)
        target = self._local(check_write(new))
        target.parent.mkdir(parents=True, exist_ok=True)
        self._local(old).replace(target)

    def delete(self, path):
        local = self._local(check_write(path))
        if local.is_file():
            local.unlink()

    def mkdirs(self, directory):
        self._local(check_write(directory.rstrip("/") + "/")).mkdir(parents=True, exist_ok=True)

    def wipe(self):
        """Empty the practice folder. Only ever used on a practice target."""
        if self.root.is_dir():
            shutil.rmtree(self.root)
        self.root.mkdir(parents=True)


class FtpTarget(Target):
    """The PS5's FTP service (anonymous, port 2121 by default)."""

    def __init__(self, host: str, port: int = 2121, timeout: int = 60):
        self.host, self.port = host, port
        self.label = f"PS5 at {host}"
        self.ftp = FTP(timeout=timeout)
        self.ftp.connect(host, port)
        self.ftp.login()
        self.ftp.voidcmd("TYPE I")
        self._made: set[str] = set()

    def close(self):
        try:
            self.ftp.quit()
        except all_errors:
            pass

    def size(self, path):
        try:
            size = self.ftp.size(clean(path))
        except all_errors:
            return None
        return None if size is None or size >= MISSING_SIZE else size

    def is_dir(self, path):
        here = self.ftp.pwd()
        try:
            self.ftp.cwd(clean(path))
            self.ftp.cwd(here)
            return True
        except all_errors:
            return False

    def read(self, path, limit=64 << 20):
        size = self.size(path)
        if size is None:
            return None
        if size > limit:
            raise ValueError(f"{path} is larger than expected")
        buffer = io.BytesIO()
        try:
            self.ftp.retrbinary(f"RETR {clean(path)}", buffer.write)
        except error_perm:
            return None
        return buffer.getvalue()

    def stream(self, path, sink):
        if self.size(path) is None:
            return False
        try:
            self.ftp.retrbinary(f"RETR {clean(path)}", sink, blocksize=1 << 16)
        except error_perm:
            return False
        return True

    def mkdirs(self, directory):
        check_write(directory.rstrip("/") + "/")
        current = ""
        for part in PurePosixPath(directory).parts[1:]:
            current += "/" + part
            if current in self._made:
                continue
            try:
                self.ftp.mkd(current)
            except all_errors:
                pass
            self._made.add(current)

    def upload(self, path, source, progress=None):
        path = check_write(path)
        self.mkdirs(str(PurePosixPath(path).parent))
        callback = (lambda block: progress(len(block))) if progress else None
        self.ftp.storbinary(f"STOR {path}", source, blocksize=1 << 16, callback=callback)

    def rename(self, old, new):
        check_write(old)
        new = check_write(new)
        self.mkdirs(str(PurePosixPath(new).parent))
        self.ftp.rename(clean(old), new)

    def delete(self, path):
        try:
            self.ftp.delete(check_write(path))
        except all_errors:
            pass


class FtpLike:
    """Lets tools written for ftplib (the branding step) work on any Target, practice folder included."""

    def __init__(self, target: Target):
        self.target = target

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def retrbinary(self, command, callback, blocksize=8192):
        if not self.target.stream(command.split(" ", 1)[1], callback):
            raise error_perm("550 not found")

    def storbinary(self, command, source, blocksize=8192, callback=None):
        self.target.upload(command.split(" ", 1)[1], source)

    def delete(self, path):
        if self.target.size(path) is None:
            raise error_perm("550 not found")
        self.target.delete(path)

    def rename(self, old, new):
        self.target.rename(old, new)

    def voidcmd(self, _command):
        return "200"


def discover(port: int = 2121, timeout: float = 0.25, prefix: str | None = None) -> list[str]:
    """Look for a PS5 FTP service on the local network. Only runs when the user asks for it."""
    import concurrent.futures
    import socket
    if prefix is None:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.connect(("192.0.2.1", 9))  # no packet is sent; this only picks the local address
            prefix = probe.getsockname()[0].rsplit(".", 1)[0]
        except OSError:
            return []
        finally:
            probe.close()

    def try_host(host):
        try:
            with socket.create_connection((host, port), timeout=timeout) as connection:
                connection.settimeout(1.0)
                return host if connection.recv(4)[:3] == b"220" else None
        except OSError:
            return None

    hosts = [f"{prefix}.{n}" for n in range(1, 255)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as pool:
        return [host for host in pool.map(try_host, hosts) if host]
