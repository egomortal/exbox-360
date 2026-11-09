#!/usr/bin/env python3
"""Unpacks the Xbox 360 system update's avatar items (AvatarAssetPack.toc) into
one file per item and block, for the private avatar import. Read only; for your
own copy of the update (FFFE07DF00000002, unpacked with stfs_extract.py). The
output is Microsoft's art: keep it in private/ and on your own console.

    python3 tools/avatar_unpack.py AvatarAssetPack.toc OUTDIR

Layout follows avatar-aura's avatar-formats (MIT), a port of Xenia's
xam/avatars (BSD 3-Clause):
  * pack: a 16-byte version GUID; asset count (BE u32) at 0x1C; v3 records of
    0x10C bytes from 0x30 (v1 0xF8 from 0x20, v2 0xF8 from 0x28). Per record
    (big-endian): categories u32 at 0, body bits at 4, asset ids at 0x9C/0xAC,
    name offsets at 0xBC (18 in v3, 13 before: [0] internal, [1] English...),
    then data offset and size;
  * data: an STRB container (optionally behind a 0x140-byte YTGR signature):
    flags at 4/5 (block alignment, little endian), id/size widths at 22/23,
    alignment at 26; blocks of (id, size, entry size). Ids: 1 animation,
    2 texture, 3 model, 4 shape overrides, 5 skeleton, 6/8 metadata,
    7 custom colour table;
  * model/texture/skeleton blocks are LZX (32 KiB window) in chunks of
    u32 compressed size, u32 output offset, u32 output size + bytes.
Writes OUTDIR/<category>/<index> <English name>.<block>.bin and
OUTDIR/index.tsv. SPDX-License-Identifier: GPL-3.0-or-later
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzx import Bits, Decoder  # noqa: E402

VERSIONS = {
    bytes.fromhex("9AD6EBCE62624EEB8A82A3F70B817369"): (0x20, 0xF8, 13),
    bytes.fromhex("8A762DF4C0674B66B4EE56466B1B8280"): (0x28, 0xF8, 13),
    bytes.fromhex("580A07D64BCD40BEBF37254BE826FB0B"): (0x30, 0x10C, 18),
}
BLOCKS = {1: "animation", 2: "texture", 3: "model", 4: "shape", 5: "skeleton",
          6: "meta", 7: "colors", 8: "meta"}
COMPRESSED = {2, 3, 5}
CATEGORY_BITS = ["head", "body", "hair", "shirt", "trousers", "shoes", "hat", "gloves",
                 "glasses", "wrist", "earrings", "ring", "carryable", "eyes", "eyebrows",
                 "mouth", "facial_hair", "facial_other", "eyeshadow", "nose", "chin", "ears",
                 "animation", "costume", "template"]


def category(bits):
    if bits == 0:
        return "other"
    if bits & (1 << 23):
        return "costume"
    if bits & (1 << 22):
        return "animation"
    if bits & 0x8 and bits & 0x200 and False:
        return "jacket"
    for i, n in enumerate(CATEGORY_BITS):
        if bits & (1 << i):
            return n
    return "mixed_%x" % bits


def name_at(d, ptr):
    if not ptr or ptr >= len(d):
        return ""
    end = ptr
    while end + 1 < len(d) and (d[end] or d[end + 1]):
        end += 2
    return d[ptr:end].decode("utf-16-be", "replace")


def records(d):
    first, stride, nnames = VERSIONS[d[:16]]
    count = struct.unpack_from(">I", d, 0x1C)[0]
    for i in range(count):
        h = d[first + i * stride:first + (i + 1) * stride]
        off, size = struct.unpack_from(">II", h, 0xBC + nnames * 4)
        names = struct.unpack_from(">%dI" % nnames, h, 0xBC)
        yield {
            "index": i,
            "categories": struct.unpack_from(">I", h, 0)[0],
            "bodies": h[4],
            "subcategory": struct.unpack_from(">I", h, 0x98)[0],
            "id": h[0x9C:0xAC].hex(),
            "internal": name_at(d, names[0]),
            "english": name_at(d, names[1]),
            "data": d[off:off + size] if off + size <= len(d) else b"",
        }


def strb_blocks(buf):
    """Yields (block id, payload) for each block of a YTGR/STRB container."""
    if buf[:4] == b"YTGR":
        buf = buf[0x140:]
    if buf[:4] != b"STRB":
        raise ValueError("not STRB")
    aligned, little = buf[4] != 0, buf[5] != 0
    idw, szw = buf[22], buf[23]
    al = buf[26] if aligned else 1

    def up(v):
        return (v + al - 1) & ~(al - 1)

    def val(o, w):
        return int.from_bytes(buf[o:o + w], "little" if little else "big")

    hdr = up(idw + 2 * szw)
    o = up(30 if aligned else 26)
    while o + hdr <= len(buf):
        bid, n, entry = val(o, idw), val(o + idw, szw), val(o + idw + szw, szw)
        o += hdr
        yield bid, buf[o:o + n * entry]
        o += up(n)


def decompress(blob):
    out = bytearray()
    p = 0
    while p + 12 <= len(blob):
        comp, at, size = struct.unpack_from("<III", blob, p)
        if comp > len(blob) - p - 12 or at != len(out):
            raise ValueError("bad LZX chunk header at %d" % p)
        out += Decoder(15).decode(Bits(blob, p + 12), size)
        p += 12 + comp
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    d = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = ["index\tcategory\tbits\tbodies\tid\tblocks\tinternal\tenglish"]
    failed = 0
    for r in records(d):
        cat = category(r["categories"])
        safe = re.sub(r'[\\/:*?"<>|]', "_", r["english"] or r["internal"])[:80]
        kinds = []
        try:
            blocks = list(strb_blocks(r["data"])) if r["data"][:4] in (b"STRB", b"YTGR") else []
        except Exception as e:  # noqa: BLE001
            print("item %d (%s): %s" % (r["index"], safe, e))
            failed += 1
            blocks = []
        seen = {}
        for bid, payload in blocks:
            kind = BLOCKS.get(bid, "b%d" % bid)
            try:
                data = decompress(payload) if bid in COMPRESSED else payload
            except Exception as e:  # noqa: BLE001
                print("item %d (%s) %s: %s" % (r["index"], safe, kind, e))
                failed += 1
                continue
            k = seen.get(kind, 0)
            seen[kind] = k + 1
            tag = kind if k == 0 else "%s%d" % (kind, k)
            kinds.append(tag)
            os.makedirs(os.path.join(out, cat), exist_ok=True)
            with open(os.path.join(out, cat, "%04d %s.%s.bin" % (r["index"], safe, tag)), "wb") as f:
                f.write(data)
        rows.append("%d\t%s\t%x\t%d\t%s\t%s\t%s\t%s" % (
            r["index"], cat, r["categories"], r["bodies"], r["id"], ",".join(kinds) or "-",
            r["internal"], r["english"]))
    with open(os.path.join(out, "index.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(rows) + "\n")
    print("unpacked to %s (%d records, %d failures)" % (out, len(rows) - 1, failed))


if __name__ == "__main__":
    main()
