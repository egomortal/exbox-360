"""Installer step: give the installed titles their names and home-screen icons.

Run by the installer after both titles are on the console (and on its own from the command line):

    python tools/branding.py 192.168.1.189 --icons xbox --loading LOADING.png --avatar AVATAR.png
    python tools/branding.py 192.168.1.189 --icons default

What it does, for Ex-Box 360 (PPSA99735) and Ex-Box Avatars (PPSA99734):
  1. Sets the name in sce_sys/param.json and in the system copy under /user/appmeta/<ID>/.
  2. Puts icon0.png in the same two places ("xbox" style is made here from your own files;
     "default" is our own artwork from installer-icons/).
  3. If the PS5 already registered the title under an older name, updates that name in the
     system app database the home screen reads (backed up first). A restart shows the change.

Every file it replaces is backed up next to the original as <name>.pre-branding the first time,
and the app database backup is also kept on your PC. It writes only these files.
"""
from __future__ import annotations

import argparse
import io
import json
import re
import sqlite3
import sys
import tempfile
import time
from ftplib import FTP, error_perm
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_ICONS = HERE.parent / "installer-icons"
TITLES = {
    "PPSA99735": {"name": "Ex-Box 360", "icon": "exbox360"},
    "PPSA99734": {"name": "Ex-Box Avatars", "icon": "avatars"},
}
APP_DB = "/system_data/priv/mms/app.db"


# ---------- FTP helpers ----------
def _connect(host: str, port: int) -> FTP:
    ftp = FTP(timeout=60)
    ftp.connect(host, port)
    ftp.login()
    ftp.voidcmd("TYPE I")
    return ftp


def _get(ftp: FTP, path: str) -> bytes | None:
    buf = io.BytesIO()
    try:
        ftp.retrbinary(f"RETR {path}", buf.write)
    except error_perm:
        return None
    return buf.getvalue()


def _put(ftp: FTP, path: str, data: bytes, backup: bool = True) -> None:
    """Write via a temporary name, keep a one-time backup, then read back to verify."""
    if backup and _get(ftp, path + ".pre-branding") is None:
        old = _get(ftp, path)
        if old is not None:
            ftp.storbinary(f"STOR {path}.pre-branding", io.BytesIO(old))
    ftp.storbinary(f"STOR {path}.part", io.BytesIO(data))
    try:
        ftp.delete(path)
    except error_perm:
        pass
    ftp.rename(f"{path}.part", path)
    if _get(ftp, path) != data:
        raise RuntimeError(f"verify failed: {path}")


# ---------- names ----------
def set_title_name(param_json: bytes, name: str) -> bytes:
    meta = json.loads(param_json)
    langs = meta.setdefault("localizedParameters", {"defaultLanguage": "en-US"})
    keys = [k for k in langs if k != "defaultLanguage"] or [langs.get("defaultLanguage", "en-US")]
    for k in keys:
        langs.setdefault(k, {})["titleName"] = name
    return (json.dumps(meta, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def rename_in_app_db(db_path: Path, renames: dict[str, str]) -> int:
    """renames maps titleId -> new name. Returns the number of changed cells."""
    db = sqlite3.connect(db_path)
    try:
        olds: dict[str, str] = {}
        for tid, new in renames.items():
            row = db.execute("select titleName from tbl_contentinfo where titleId=?", (tid,)).fetchone()
            if not row or row[0] == new:
                continue
            old = row[0]
            # Only rename a string that belongs to this one title, so no other app is touched.
            if db.execute("select count(*) from tbl_contentinfo where titleName=?", (old,)).fetchone()[0] != 1:
                continue
            olds[old] = new
        if not olds:
            return 0
        changed = 0
        tables = [r[0] for r in db.execute("select name from sqlite_master where type='table'")]
        for table in tables:
            for col in [c[1] for c in db.execute(f'pragma table_info("{table}")')]:
                for old, new in olds.items():
                    rows = db.execute(
                        f'select rowid,"{col}" from "{table}" where typeof("{col}")=\'text\' and instr("{col}",?)>0',
                        (old,)).fetchall()
                    for rowid, value in rows:
                        if value == old:
                            fixed = new
                        else:  # JSON text: replace the exact quoted string and fix stored byte sizes
                            fixed = value.replace(json.dumps(old, ensure_ascii=False), json.dumps(new, ensure_ascii=False))
                            fixed = re.sub(r'("data":%s,"key":"[^"]+","size":)\d+' % re.escape(json.dumps(new, ensure_ascii=False)),
                                           lambda m: m.group(1) + str(len(new.encode("utf-8"))), fixed)
                        if fixed != value:
                            db.execute(f'update "{table}" set "{col}"=? where rowid=?', (fixed, rowid))
                            changed += 1
        if db.execute("pragma integrity_check").fetchone()[0] != "ok":
            raise RuntimeError("app database check failed; nothing was uploaded")
        db.commit()
        return changed
    finally:
        db.close()


# ---------- icons ----------
def make_icons(style: str, loading: Path | None, avatar: Path | None, out: Path) -> dict[str, Path]:
    icons = {t: DEFAULT_ICONS / f"{v['icon']}-default.png" for t, v in TITLES.items()}
    if style == "xbox":
        if not loading or not loading.is_file():
            print("Xbox-style icons need your loading-screen art; using the Ex-Box default icons.")
            return icons
        sys.path.insert(0, str(HERE))
        from PIL import Image
        import make_app_icons as m
        src = Image.open(loading)
        m.xbox_dashboard(src, out / "exbox360-xbox.png")
        icons["PPSA99735"] = out / "exbox360-xbox.png"
        if avatar and avatar.is_file():
            m.xbox_avatars(src, Image.open(avatar), out / "avatars-xbox.png")
            icons["PPSA99734"] = out / "avatars-xbox.png"
    return icons


# ---------- main step ----------
def apply_branding(host: str, port: int = 2121, style: str = "xbox", loading: Path | None = None,
                   avatar: Path | None = None, backup_dir: Path | None = None, log=print,
                   connection=None) -> dict:
    """connection: an already-open FTP-style object (the installer passes its own, so a practice
    run can use a folder on the PC). Left out, this connects to host:port itself."""
    result = {"titles": {}, "app_db_changes": 0, "restart_needed": False}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        icons = make_icons(style, loading, avatar, tmp)
        with (connection if connection is not None else _connect(host, port)) as ftp:
            for tid, info in TITLES.items():
                base = f"/data/homebrew/{tid}/sce_sys"
                param = _get(ftp, f"{base}/param.json")
                if param is None:
                    log(f"{info['name']}: not installed, skipped")
                    continue
                icon = icons[tid].read_bytes()
                for folder in (base, f"/user/appmeta/{tid}"):
                    p = _get(ftp, f"{folder}/param.json")
                    if p is not None:
                        _put(ftp, f"{folder}/param.json", set_title_name(p, info["name"]))
                        _put(ftp, f"{folder}/icon0.png", icon)
                result["titles"][tid] = info["name"]
                log(f"{info['name']}: name and icon set")

            original = _get(ftp, APP_DB)
            if original:
                work = tmp / "app.db"
                work.write_bytes(original)
                changed = rename_in_app_db(work, {t: TITLES[t]["name"] for t in result["titles"]})
                if changed:
                    if backup_dir:
                        backup_dir.mkdir(parents=True, exist_ok=True)
                        (backup_dir / f"app.db.pre-branding-{time.strftime('%Y%m%dT%H%M%S')}").write_bytes(original)
                    if _get(ftp, APP_DB) != original:
                        raise RuntimeError("the app database changed while working; try again")
                    stamp = time.strftime("%Y%m%dT%H%M%S")
                    ftp.storbinary(f"STOR {APP_DB}.pre-branding-{stamp}", io.BytesIO(original))
                    new = work.read_bytes()
                    ftp.storbinary(f"STOR {APP_DB}", io.BytesIO(new))
                    if _get(ftp, APP_DB) != new:
                        ftp.storbinary(f"STOR {APP_DB}", io.BytesIO(original))
                        raise RuntimeError("app database verify failed; the original was put back")
                    result["app_db_changes"] = changed
                    result["restart_needed"] = True
                    log("Home-screen names updated. Restart the PS5 to see them.")
    return result


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--icons", choices=["xbox", "default"], default="xbox")
    ap.add_argument("--loading", type=Path, help="your loading-screen image (for Xbox-style icons)")
    ap.add_argument("--avatar", type=Path, help="a render of your avatar on a transparent background")
    ap.add_argument("--backup-dir", type=Path, default=HERE.parent / "private" / "backups")
    a = ap.parse_args()
    print(json.dumps(apply_branding(a.host, a.port, a.icons, a.loading, a.avatar, a.backup_dir), indent=2))


if __name__ == "__main__":
    main()
