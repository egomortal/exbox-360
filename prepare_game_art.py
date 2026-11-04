"""Cache Xbox artwork for installed PS4/PS5 games with matching names.

The console retains its own icon for games without an exact Xbox 360 match.
Run explicitly on a PC, then use deploy.py --no-elf to stage the saved covers.
"""

import argparse
from ftplib import FTP
from io import BytesIO
import json
from pathlib import Path
import re
import struct
from urllib.parse import quote, urlsplit
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "private" / "game_art"
ID = re.compile(r"^(?:CUSA|PPSA)\d{5}$")


def normalized(title):
    title = re.sub(r"\s*\((?:PS4|PS5|Xbox 360)\)\s*$", "", title, flags=re.I)
    return re.sub(r"[^a-z0-9]+", "", title.casefold())


def sfo_title(data):
    if len(data) < 20 or data[:4] != b"\0PSF":
        return ""
    keys, values, count = struct.unpack_from("<III", data, 8)
    for i in range(min(count, 256)):
        at = 20 + i * 16
        if at + 16 > len(data):
            break
        key_off, _, length, _, value_off = struct.unpack_from("<HHIII", data, at)
        if keys + key_off >= len(data) or values + value_off >= len(data):
            continue
        key = data[keys + key_off:].split(b"\0", 1)[0]
        if key == b"TITLE":
            raw = data[values + value_off:values + value_off + length].split(b"\0", 1)[0]
            return raw.decode("utf-8", "replace").strip()
    return ""


def json_title(data):
    try:
        doc = json.loads(data)
    except (ValueError, UnicodeDecodeError):
        return ""
    if not isinstance(doc, dict):
        return ""
    localized = doc.get("localizedParameters") or {}
    if isinstance(localized, dict):
        default = localized.get("defaultLanguage", "")
        preferred = localized.get(default, {})
        if isinstance(preferred, dict) and preferred.get("titleName"):
            return preferred["titleName"].strip()
        for value in localized.values():
            if isinstance(value, dict) and value.get("titleName"):
                return value["titleName"].strip()
    return str(doc.get("titleName") or "").strip()


def remote_bytes(ftp, path):
    chunks = BytesIO()
    try:
        ftp.retrbinary("RETR " + path, chunks.write)
    except Exception:
        return b""
    return chunks.getvalue()


def console_titles(host):
    names = {}
    with FTP(timeout=15) as ftp:
        ftp.connect(host, 2121)
        ftp.login("anonymous", "codex")
        for root in ("/user/appmeta", "/system_data/priv/appmeta"):
            try:
                ftp.cwd(root)
                ids = [name for name, info in ftp.mlsd()
                       if ID.fullmatch(name) and info.get("type") == "dir"]
            except Exception as exc:
                print(f"Cannot list {root}: {exc}")
                continue
            for title_id in ids:
                base = f"{root}/{title_id}"
                name = json_title(remote_bytes(ftp, base + "/param.json"))
                if not name:
                    name = sfo_title(remote_bytes(ftp, base + "/param.sfo"))
                if name:
                    names[title_id] = name
    return names


def get_url(url):
    req = Request(url, headers={"User-Agent": "Ex-Box 360 private artwork cache"})
    with urlopen(req, timeout=20) as response:
        data = response.read(8 * 1024 * 1024 + 1)
    if len(data) > 8 * 1024 * 1024:
        raise ValueError("image exceeds 8 MiB")
    return data


def xboxunity_cover(title):
    url = "https://www.xboxunity.net/api/Covers/" + quote(title)
    choices = json.loads(get_url(url))
    matches = [c for c in choices if normalized(str(c.get("name", ""))) == normalized(title)]
    if not matches:
        return None
    matches.sort(key=lambda c: (not bool(c.get("official")), -int(c.get("rating") or 0)))
    return matches[0]


def save_cover(title_id, title, choice):
    url = choice.get("front") or choice.get("url") or ""
    parsed = urlsplit(url)
    if parsed.hostname not in ("assets.xboxunity.net", "www.xboxunity.net", "xboxunity.net"):
        raise ValueError("unexpected artwork host")
    data = get_url(url)
    if data.startswith(b"\xff\xd8\xff"):
        ext = ".jpg"
    elif data.startswith(b"\x89PNG\r\n\x1a\n"):
        ext = ".png"
    else:
        raise ValueError("response is not a JPEG or PNG")
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / (title_id + ext)
    target.write_bytes(data)
    return {"playstation_id": title_id, "title": title,
            "xbox_title": choice["name"], "xbox_title_id": choice["titleid"],
            "source": "XboxUnity", "url": url, "file": target.name}


def xbox_store_cover(title):
    """Find an exact name in the public Xbox store catalog, including Xbox One."""
    first = normalized(title)[:1]
    if not first or not first.isalpha():
        return None
    url = ("https://raw.githubusercontent.com/Ephellon/game-store-catalog/"
           f"main/xbox-console/{first}.json")
    choices = json.loads(get_url(url))
    matches = [c for c in choices if isinstance(c, dict)
               and normalized(str(c.get("name", ""))) == normalized(title)
               and c.get("type") in ("game", "Product")
               and any(p.startswith("Xbox") for p in c.get("platforms", []))]
    def platform_rank(choice):
        platforms = choice["platforms"]
        if "Xbox One" in platforms:
            return 0
        if "Xbox Series X|S" in platforms:
            return 1
        return 2

    matches.sort(key=lambda c: (platform_rank(c), str(c.get("uuid", ""))))
    return matches[0] if matches else None


def save_store_cover(title_id, title, choice):
    product_id = str(choice.get("uuid") or "")
    if not re.fullmatch(r"[A-Z0-9]{10,16}", product_id):
        raise ValueError("invalid Xbox store product ID")
    catalog_url = ("https://displaycatalog.mp.microsoft.com/v7.0/products"
                   f"?actionFilter=Browse&bigIds={product_id}&fieldsTemplate=Details"
                   "&languages=en-US&market=US")
    catalog = json.loads(get_url(catalog_url))
    products = catalog.get("Products") or []
    if not products or products[0].get("ProductId") != product_id:
        raise ValueError("Xbox store product lookup failed")
    properties = products[0].get("LocalizedProperties") or []
    images = properties[0].get("Images", []) if properties else []
    preferred = next((i for kind in ("BoxArt", "Poster") for i in images
                      if i.get("ImagePurpose") == kind and i.get("Uri")), None)
    if preferred is None:
        raise ValueError("Xbox store has no box or poster art")
    url = str(preferred["Uri"])
    if url.startswith("//"):
        url = "https:" + url
    if urlsplit(url).hostname != "store-images.s-microsoft.com":
        raise ValueError("unexpected Xbox store artwork host")
    data = get_url(url + ("&" if "?" in url else "?") + "w=600")
    if data.startswith(b"\xff\xd8\xff"):
        ext = ".jpg"
    elif data.startswith(b"\x89PNG\r\n\x1a\n"):
        ext = ".png"
    else:
        raise ValueError("response is not a JPEG or PNG")
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / (title_id + ext)
    target.write_bytes(data)
    return {"playstation_id": title_id, "title": title,
            "xbox_title": choice["name"], "xbox_store_id": product_id,
            "xbox_platforms": choice["platforms"], "source": "Xbox Store",
            "url": url, "source_page": choice.get("href", ""), "file": target.name}


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", help="PS5 FTP host; reads installed title names")
    parser.add_argument("--title", action="append", default=[], metavar="CUSA12345=Game Name",
                        help="add a game without connecting to the console")
    parser.add_argument("--output", type=Path, default=OUT, help="local folder for the artwork cache")
    parser.add_argument("--refresh", action="store_true", help="replace existing cached matches")
    args = parser.parse_args()
    OUT = args.output
    if not args.host and not args.title:
        parser.error("provide --host or at least one --title")
    titles = console_titles(args.host) if args.host else {}
    for item in args.title:
        title_id, sep, title = item.partition("=")
        if not sep or not ID.fullmatch(title_id) or not title.strip():
            parser.error("--title must be CUSA12345=Game Name or PPSA12345=Game Name")
        titles[title_id] = title.strip()
    OUT.mkdir(parents=True, exist_ok=True)
    manifest_path = OUT / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        manifest = {}
    for title_id, title in sorted(titles.items()):
        if not args.refresh and title_id in manifest and (OUT / manifest[title_id].get("file", "")).is_file():
            print(f"{title_id}: cached {title}")
            continue
        try:
            # A PS4/PS5 release is visually closer to its Xbox One/Series
            # counterpart. Keep XboxUnity's 360 cover as a fallback.
            try:
                cover = xbox_store_cover(title)
                if cover is not None:
                    manifest[title_id] = save_store_cover(title_id, title, cover)
                    print(f"{title_id}: saved {title} from Xbox Store")
                    continue
            except Exception as exc:
                print(f"{title_id}: Xbox Store unavailable ({exc}); trying XboxUnity")
            cover = xboxunity_cover(title)
            if cover is None:
                print(f"{title_id}: no exact Xbox cover for {title}; using PlayStation art")
                continue
            manifest[title_id] = save_cover(title_id, title, cover)
            print(f"{title_id}: saved {title} from XboxUnity")
        except Exception as exc:
            print(f"{title_id}: {exc}; using PS5 art")
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
