"""Process cover requests written by the Ex-Box Details menu.

Run with --host on the PC to collect the PS5's queued cover requests, fetch
exact-title matches through prepare_game_art.py, and stage the covers. Or pass
an exported media_requests.tsv file to stay entirely offline from the PS5.
"""

import argparse
from ftplib import FTP
from io import BytesIO
import json
from pathlib import Path
import re
import subprocess
import sys


TITLE_ID = re.compile(r"(?:CUSA|PPSA)\d{5}\Z")


def requests(lines: str) -> list[str]:
    items = {}
    for line in lines.splitlines():
        title_id, sep, title = line.partition("\t")
        title = title.strip()
        if sep and TITLE_ID.fullmatch(title_id) and title and "\t" not in title:
            items[title_id] = title
    return [f"{title_id}={title}" for title_id, title in sorted(items.items())]


def fetch_queue(host: str) -> str:
    data = BytesIO()
    with FTP(timeout=20) as ftp:
        ftp.connect(host, 2121)
        ftp.login("anonymous", "codex")
        ftp.retrbinary("RETR /data/nxe/media_requests.tsv", data.write)
    return data.getvalue().decode("utf-8", "replace")


def upload_covers(host: str, titles: list[str], output: Path) -> None:
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    with FTP(timeout=30) as ftp:
        ftp.connect(host, 2121)
        ftp.login("anonymous", "codex")
        try:
            ftp.mkd("/data/homebrew/ExBox360/game_art")
        except Exception:
            pass  # already exists
        for title in titles:
            title_id = title.partition("=")[0]
            entry = manifest.get(title_id, {})
            filename = entry.get("file", "")
            if filename not in (title_id + ".jpg", title_id + ".png"):
                print(f"{title_id}: no exact matching cover to upload")
                continue
            cover = output / filename
            if not cover.is_file():
                continue
            destination = "/data/homebrew/ExBox360/game_art/" + filename
            temporary = destination + ".part"
            with cover.open("rb") as data:
                ftp.storbinary("STOR " + temporary, data)
            ftp.rename(temporary, destination)
            print(f"{title_id}: uploaded {filename}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("queue", type=Path, nargs="?", help="exported media_requests.tsv")
    parser.add_argument("--host", help="PS5 FTP address; fetch requests and stage matched covers")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "private" / "game_art")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not args.queue and not args.host:
        parser.error("provide a queue file or --host")
    lines = args.queue.read_text(encoding="utf-8") if args.queue else fetch_queue(args.host)
    titles = requests(lines)
    if not titles:
        print("No valid game cover requests found.")
        return 1
    command = [sys.executable, str(Path(__file__).resolve().parent / "prepare_game_art.py"),
               "--output", str(args.output)]
    for title in titles:
        command.extend(("--title", title))
    if args.dry_run:
        print("\n".join(titles))
        return 0
    result = subprocess.run(command, check=False)
    if result.returncode == 0:
        if args.host:
            upload_covers(args.host, titles, args.output)
            print("In Ex-Box, select Refresh Covers and Trailers to show the new art.")
        else:
            print("Stage the saved covers with: python deploy.py <PS5-IP> --no-elf --resume")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
