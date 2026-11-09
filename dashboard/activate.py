"""Switch a staged dashboard ELF into place while the dashboard is closed."""
from ftplib import FTP
from datetime import datetime, timezone
import sys

host = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.189"
root = "/data/homebrew/ExBox360"
backup = f"{root}/eboot.backup.{datetime.now(timezone.utc):%Y%m%d%H%M%S}"
with FTP(timeout=15) as ftp:
    ftp.connect(host, 2121)
    ftp.login("anonymous", "codex")
    ftp.rename(f"{root}/eboot.elf", backup)
    try:
        ftp.rename(f"{root}/eboot.next", f"{root}/eboot.elf")
    except Exception:
        ftp.rename(backup, f"{root}/eboot.elf")
        raise
print(f"Activated staged dashboard; prior ELF backed up at {backup}")
