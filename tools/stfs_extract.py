#!/usr/bin/env python3
"""List or extract an Xbox 360 STFS package (CON / LIVE / PIRS), such as the
system update's FFFE07DF000000xx packs (AvatarAsset, DashPack, NuiPack ...).

    python3 stfs_extract.py PACKAGE              list the files
    python3 stfs_extract.py PACKAGE OUTDIR       extract them (OUTDIR must not exist yet)

Layout (as documented by Velocity / py360): the header size is at 0x340, the
volume descriptor at 0x379. Data blocks are 4 KiB, starting at the first 4 KiB
boundary after the header, with a hash table before every 0xAA data blocks
(and every 0x70E4 at the second level); a package with the "female" block
separation (bit 0 set) has one-block hash tables, otherwise two. File table
entries are 0x40 bytes. Non-contiguous files follow the next-block numbers in
the level-0 hash tables. Read only: the package is never modified.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import os
import struct
import sys


def i24le(b, o):
    return b[o] | b[o + 1] << 8 | b[o + 2] << 16


def i24be(b, o):
    return b[o] << 16 | b[o + 1] << 8 | b[o + 2]


class Stfs:
    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(0x971A)
        self.magic = head[:4]
        if self.magic not in (b"CON ", b"LIVE", b"PIRS"):
            raise ValueError("not an STFS package: %r" % self.magic)
        self.header_size = struct.unpack(">I", head[0x340:0x344])[0]
        vd = head[0x379:0x379 + 0x24]
        self.separation = vd[2]
        self.table_blocks = struct.unpack("<H", vd[3:5])[0]
        self.table_block = i24le(vd, 5)
        self.shift = 0 if (self.separation & 1) else 1   # female: one-block hash tables
        self.first_table = (self.header_size + 0xFFF) & 0xFFFFF000
        self.display_name = head[0x411:0x411 + 0x80].decode("utf-16-be", "replace").rstrip("\x00")

    def backing(self, block):
        # Data blocks skip one level-0 hash table per 0xAA blocks and, from block
        # 0xAA on, the level-1 tables too (one per 0x70E4 blocks), then the
        # level-2 table (Velocity's ComputeBackingDataBlockNumber).
        r = (((block + 0xAA) // 0xAA) << self.shift) + block
        if block < 0xAA:
            return r
        if block < 0x70E4:
            return r + (((block + 0x70E4) // 0x70E4) << self.shift)
        return (1 << self.shift) + r + (((block + 0x70E4) // 0x70E4) << self.shift)

    def address(self, block):
        return (self.backing(block) << 12) + self.first_table

    def read_block(self, block):
        self.f.seek(self.address(block))
        return self.f.read(0x1000)

    def hash_entry(self, block):
        """Level-0 hash table entry for a data block: (status, next block)."""
        base = block // 0xAA * 0xAA
        table = self.backing(base) - (1 << self.shift)
        self.f.seek((table << 12) + self.first_table + (block % 0xAA) * 0x18)
        e = self.f.read(0x18)
        return e[0x14], i24be(e, 0x15)

    def files(self):
        out = []
        block = self.table_block
        for _ in range(self.table_blocks):
            data = self.read_block(block)
            for i in range(0, 0x1000, 0x40):
                e = data[i:i + 0x40]
                flags = e[0x28]
                if not flags & 0x3F:
                    continue
                name = e[:flags & 0x3F].decode("latin-1")
                out.append({
                    "name": name,
                    "dir": bool(flags & 0x80),
                    "contiguous": bool(flags & 0x40),
                    "blocks": i24le(e, 0x29),
                    "start": i24le(e, 0x2F),
                    "parent": struct.unpack(">h", e[0x32:0x34])[0],
                    "size": struct.unpack(">I", e[0x34:0x38])[0],
                })
            block = self.hash_entry(block)[1]
        for f in out:
            parts, p, guard = [f["name"]], f["parent"], 0
            while p != -1 and 0 <= p < len(out) and guard < 64:
                parts.insert(0, out[p]["name"])
                p, guard = out[p]["parent"], guard + 1
            f["path"] = "/".join(parts)
        return out

    def read_file(self, f):
        data, block, left = bytearray(), f["start"], f["size"]
        for _ in range(f["blocks"]):
            if left <= 0:
                break
            chunk = self.read_block(block)[:min(0x1000, left)]
            data += chunk
            left -= len(chunk)
            block = block + 1 if f["contiguous"] else self.hash_entry(block)[1]
        return bytes(data)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    pkg = Stfs(sys.argv[1])
    files = pkg.files()
    print("%s  %s  %d entries" % (pkg.magic.decode(), pkg.display_name, len(files)))
    if len(sys.argv) < 3:
        for f in files:
            print("%s %10d  %s" % ("d" if f["dir"] else "-", f["size"], f["path"]))
        return
    out = sys.argv[2]
    os.makedirs(out)
    root = os.path.realpath(out)
    for f in files:
        target = os.path.realpath(os.path.join(out, f["path"]))
        if not target.startswith(root + os.sep):
            print("skipped (outside the folder):", f["path"])
            continue
        if f["dir"]:
            os.makedirs(target, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as w:
            w.write(pkg.read_file(f))
    print("extracted to", out)


if __name__ == "__main__":
    main()
