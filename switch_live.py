"""Stop only Ex-Box 360, then activate its staged ELF before auto-return."""
from ftplib import FTP
from datetime import datetime, timezone
import json
import sys
import time
from urllib.request import Request, urlopen

host = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.189"
api = f"http://{host}:8084"
with urlopen(f"{api}/processes_list", timeout=5) as response:
    processes = json.load(response)["processes"]
dashboards = [p for p in processes if p["name"] == "eboot.elf"]
if len(dashboards) > 1:
    raise RuntimeError("More than one eboot.elf process; refusing to guess")
if dashboards:
    pid = dashboards[0]["pid"]
    with urlopen(Request(f"{api}/process_kill?pid={pid}", method="POST"), timeout=5) as response:
        print(f"Stopped dashboard PID {pid}: {response.status}")
    for _ in range(20):
        with urlopen(f"{api}/processes_list", timeout=5) as response:
            if not any(p["name"] == "eboot.elf" for p in json.load(response)["processes"]):
                break
        time.sleep(0.1)
    else:
        raise RuntimeError("Dashboard did not stop")
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
print(f"Activated updated dashboard; previous ELF: {backup}")
