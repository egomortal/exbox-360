"""Stage and atomically replace Ex-Box native binaries over the PS5 FTP service."""

import argparse
from datetime import datetime, timezone
from ftplib import FTP
from io import BytesIO
from pathlib import Path


ROOT = Path(__file__).resolve().parent
FILES = [
    (ROOT / "build/eboot.elf", "/data/homebrew/ExBox360/eboot.elf"),
    (ROOT.parent / "native-daemon/build/nxe_dashboard_daemon.elf", "/data/pldmgr/exbox360-daemon.elf"),
]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host", help="PS5 LAN address")
    parser.add_argument("--port", type=int, default=2121)
    parser.add_argument("--app-only", action="store_true", help="leave resident launcher file as is")
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")

    with FTP() as ftp:
        ftp.connect(args.host, args.port, timeout=20)
        ftp.login()
        for local, remote in FILES[:1] if args.app_only else FILES:
            data = local.read_bytes()
            staging = remote + ".new"
            backup = remote + ".backup." + stamp
            ftp.storbinary("STOR " + staging, BytesIO(data))
            size = ftp.size(staging)
            if size != len(data):
                raise RuntimeError(f"staged size mismatch for {remote}: {size} != {len(data)}")
            ftp.rename(remote, backup)
            try:
                ftp.rename(staging, remote)
            except Exception:
                ftp.rename(backup, remote)
                raise
            print(f"updated {remote}: {len(data)} bytes; backup {backup}")


if __name__ == "__main__":
    main()
