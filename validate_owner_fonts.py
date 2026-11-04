"""Independently parse every recovered glyph and verify source outline bytes."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "audit-deps"))
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent
results = []
for source_name, target_name in (("xenonclatin.xtt", "Convection.ttf"),
                                  ("XenonSCLatin.xtt", "SegoeXbox.ttf")):
    folder = ROOT / "private/fonts-owner"
    source = next(p for p in folder.iterdir() if p.name.lower() == source_name.lower())
    target = folder / target_name
    raw = source.read_bytes()
    metadata = zlib.decompress(raw[0x118:])
    records = {}
    for i in range(struct.unpack_from(">H", metadata, 4)[0]):
        tag, _, offset, length = struct.unpack_from(">4sIII", metadata, 12 + i * 16)
        records[tag] = (offset, length)
    offset, length = records[b"xloc"]
    locations = struct.unpack(">" + "I" * (length // 4), metadata[offset:offset+length])
    start = records[b"xglf"][0]
    outlines = b"".join(zlib.decompress(raw[start+i*4096:start+(i+1)*4096])
                        for i in range((locations[-1] >> 16) + 1))
    font = TTFont(target, checkChecksums=2, recalcBBoxes=False)
    assert font.reader["glyf"] == outlines, "Source outline bytes changed"
    order = font.getGlyphOrder()
    composite = 0
    for name in order:
        glyph = font["glyf"][name]
        if glyph.isComposite(): composite += 1
        glyph.getCoordinates(font["glyf"])
    mapped = font.getBestCmap()
    assert all(ord(c) in mapped for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789&?!")
    results.append({"font": target_name, "glyphs_validated": len(order),
                    "composites": composite, "mapped_characters": len(mapped),
                    "source_outline_bytes_identical": True,
                    "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
    font.close()
report = ROOT / "build/audit-20261025/fonts.json"
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
