"""T13: read-only XDBF extraction and exact-build provenance verification.

All owner-derived output and evidence stay private. Never delete stale files.
"""
import hashlib
import json
from pathlib import Path
import struct
import zlib

from build_era_library import ROOT, RESOURCES, unpack


def png_entry(raw):
    if len(raw) < 24 or raw[:4] != b"XDBF":
        raise ValueError("Not XDBF")
    _, version, capacity, count, free_capacity, _ = struct.unpack_from(">6I", raw)
    if version != 0x10000 or count > capacity:
        raise ValueError("Invalid XDBF header")
    base = 24 + capacity * 18 + free_capacity * 8
    images = []
    for i in range(count):
        namespace, ident, offset, size = struct.unpack_from(">HQII", raw, 24 + i * 18)
        data = raw[base + offset:base + offset + size]
        if len(data) != size:
            raise ValueError("Truncated XDBF entry")
        if namespace != 2 or not data.startswith(b"\x89PNG\r\n\x1a\n"):
            continue
        at = 8
        while at < len(data):
            length = struct.unpack_from(">I", data, at)[0]
            chunk = data[at + 4:at + 8 + length]
            crc = struct.unpack_from(">I", data, at + 8 + length)[0]
            if zlib.crc32(chunk) != crc:
                raise ValueError("PNG CRC mismatch")
            at += length + 12
            if chunk[:4] == b"IEND":
                break
        if at != len(data) or chunk[:4] != b"IEND":
            raise ValueError("Invalid PNG end")
        images.append((ident, data))
    if len(images) != 1:
        raise ValueError(f"Expected one PNG, got {len(images)}")
    return images[0]


def main():
    evidence = {"images": [], "provenance": []}
    out = ROOT / "private/t13-source/extraction.json"
    previous = {r["file"]: r for r in json.loads(out.read_text())["provenance"]} if out.exists() else {}
    builds = {1888: "Blades/Retail/1888", 6770: "Blades/Retail/6770",
              9199: "NXE/Retail/9199", 13604: "Kinect/Retail/13604",
              15574: "Metro/V1/Retail/15574", 17559: "Metro/V2/Retail/17559"}
    for build, directory in builds.items():
        source = RESOURCES / directory / "dash.xex/FFFE07D1"
        ident, data = png_entry(source.read_bytes())
        dest = ROOT / f"private/era-skins/{build}/dashboard-title.png"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        evidence["images"].append({"file": str(dest.relative_to(ROOT)),
            "source": str(source), "entry_id": ident, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()})
    path = ROOT / "private/era-skins/sources.json"
    original = path.read_bytes()
    rows = json.loads(original)
    for build in (7371, 8495, 12413, 12607, 13142, 13146, 13599):
        directories = [p for p in RESOURCES.rglob(str(build)) if (p / "dash.xex").is_dir()]
        if len(directories) != 1:
            raise ValueError(f"Ambiguous/missing build: {build}")
        directory = directories[0]
        packages = {}
        for p in (directory / "dash.xex").iterdir():
            if p.is_file() and p.read_bytes()[:4] == b"XUIZ":
                packages[p.name] = unpack(p.read_bytes())
        for row in rows:
            if row["build"] != build or (row["exact_build"] and row["file"] not in previous):
                continue
            parts = row["source"].split("/")
            module, member = parts[-2:]
            data = packages.get(module, {}).get(member)
            actual = (ROOT / "private/era-skins" / row["file"]).read_bytes()
            match = data is not None and data == actual
            evidence["provenance"].append({"file": row["file"], "previous_source": previous.get(row["file"], {}).get("previous_source", row["source"]),
                "exact": match, "sha256": hashlib.sha256(actual).hexdigest(),
                "reason": "byte match" if match else "member absent" if data is None else "bytes differ"})
            if match:
                row.update(exact_build=True, source_build=build,
                    source=f"decrypted/resources/{directory.relative_to(RESOURCES).as_posix()}/dash.xex/{module}/{member}",
                    sha256=hashlib.sha256(data).hexdigest())
    newline = "\r\n" if b"\r\n" in original else "\n"
    path.write_bytes((json.dumps(rows, indent=2) + "\n").replace("\n", newline).encode())
    out.parent.mkdir(parents=True, exist_ok=True)
    # Preserve the original verification evidence on an idempotent rerun.
    if out.exists() and not evidence["provenance"]:
        evidence["provenance"] = json.loads(out.read_text())["provenance"]
    out.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"Extracted {len(evidence['images'])} images; verified "
          f"{sum(r['exact'] for r in evidence['provenance'])}/{len(evidence['provenance'])} fallback entries")


if __name__ == "__main__":
    main()
