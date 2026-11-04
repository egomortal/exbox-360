"""LZX decoder (the CAB / Xbox 360 XMemCompress flavour), written for Ex-Box 360
from the published LZX format description (and checked against libmspack's
lzxd.c by reading it). Pure Python; fast enough for avatar records.

    Decoder(window_bits).decode(Bits(data, start), out_size) -> bytes

Bitstream: 16-bit little-endian words, bits taken most significant first.
SPDX-License-Identifier: GPL-3.0-or-later
"""

MIN_MATCH = 2
NUM_CHARS = 256
PRETREE_NUM = 20
ALIGNED_NUM = 8
LENGTH_NUM = 249

EXTRA_BITS = []
POSITION_BASE = []
_j = 0
for _i in range(0, 51, 2):
    EXTRA_BITS += [_j, _j]
    if _i != 0 and _j < 17:
        _j += 1
EXTRA_BITS = EXTRA_BITS[:51]
_b = 0
for _i in range(51):
    POSITION_BASE.append(_b)
    _b += 1 << EXTRA_BITS[_i]


class LzxError(Exception):
    pass


class Bits:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos
        self.buf = 0
        self.n = 0

    def _word(self):
        if self.p + 1 < len(self.d):
            w = self.d[self.p] | (self.d[self.p + 1] << 8)
        elif self.p < len(self.d):
            w = self.d[self.p]
        else:
            w = 0
            if self.p > len(self.d) + 8:
                raise LzxError("past end")
        self.p += 2
        return w

    def ensure(self, k):
        while self.n < k:
            self.buf = (self.buf << 16) | self._word()
            self.n += 16

    def peek(self, k):
        self.ensure(k)
        return (self.buf >> (self.n - k)) & ((1 << k) - 1)

    def read(self, k):
        if k == 0:
            return 0
        v = self.peek(k)
        self.n -= k
        self.buf &= (1 << self.n) - 1
        return v

    def align16(self):
        self.n -= self.n % 16 if self.n % 16 else 0
        self.buf &= (1 << self.n) - 1

    def byte_pos(self):
        # Position of the next unread whole 16-bit word.
        return self.p - (self.n // 16) * 2


class Huff:
    def __init__(self, lengths):
        self.lengths = lengths
        maxlen = max(lengths) if lengths else 0
        self.maxlen = maxlen
        self.table = {}
        if maxlen == 0:
            return
        # Canonical codes.
        code = 0
        bl_count = [0] * (maxlen + 1)
        for l in lengths:
            if l:
                bl_count[l] += 1
        next_code = [0] * (maxlen + 2)
        for bits in range(1, maxlen + 1):
            code = (code + bl_count[bits - 1]) << 1
            next_code[bits] = code
        for sym, l in enumerate(lengths):
            if l:
                self.table[(l, next_code[l])] = sym
                next_code[l] += 1
        # Fast table keyed by maxlen-bit prefix.
        self.fast = [None] * (1 << maxlen)
        for (l, c), sym in self.table.items():
            base = c << (maxlen - l)
            for k in range(1 << (maxlen - l)):
                self.fast[base + k] = (sym, l)
        if None in self.fast and sum(1 for x in self.fast if x is None) == len(self.fast):
            raise LzxError("empty tree")

    def decode(self, bits):
        if self.maxlen == 0:
            raise LzxError("decode with empty tree")
        e = self.fast[bits.peek(self.maxlen)]
        if e is None:
            raise LzxError("bad code")
        bits.read(e[1])
        return e[0]


def read_lengths(bits, lens, first, last):
    pre = Huff([bits.read(4) for _ in range(PRETREE_NUM)])
    x = first
    while x < last:
        z = pre.decode(bits)
        if z == 17:
            y = bits.read(4) + 4
            for _ in range(y):
                if x < last:
                    lens[x] = 0
                    x += 1
        elif z == 18:
            y = bits.read(5) + 20
            for _ in range(y):
                if x < last:
                    lens[x] = 0
                    x += 1
        elif z == 19:
            y = bits.read(1) + 4
            z = pre.decode(bits)
            z = (lens[x] - z + 17) % 17
            for _ in range(y):
                if x < last:
                    lens[x] = z
                    x += 1
        else:
            lens[x] = (lens[x] - z + 17) % 17
            x += 1


def position_slots(window_bits):
    if window_bits == 20:
        return 42
    if window_bits == 21:
        return 50
    return window_bits * 2


class Decoder:
    def __init__(self, window_bits):
        self.wb = window_bits
        self.wsize = 1 << window_bits
        self.npos = position_slots(window_bits)
        self.main_len = [0] * (NUM_CHARS + self.npos * 8)
        self.length_len = [0] * LENGTH_NUM
        self.R = [1, 1, 1]
        self.window = bytearray()
        self.header_read = False
        self.block_remaining = 0
        self.block_type = 0
        self.intel = False

    def decode(self, bits, out_len):
        """Decodes out_len bytes from the bit reader (continuing state)."""
        out = self.window
        start = len(out)
        if not self.header_read:
            if bits.read(1):
                self.intel = True
                bits.read(16)
                bits.read(16)
            self.header_read = True
        target = start + out_len
        while len(out) < target:
            if self.block_remaining == 0:
                if self.block_type == 3 and (self.block_len & 1):
                    bits.p += 1  # realign after an odd uncompressed block
                self.block_type = bits.read(3)
                hi = bits.read(16)
                lo = bits.read(8)
                self.block_remaining = self.block_len = (hi << 8) | lo
                if self.block_type == 2:
                    self.aligned = Huff([bits.read(3) for _ in range(ALIGNED_NUM)])
                if self.block_type in (1, 2):
                    read_lengths(bits, self.main_len, 0, NUM_CHARS)
                    read_lengths(bits, self.main_len, NUM_CHARS, len(self.main_len))
                    self.main = Huff(self.main_len)
                    read_lengths(bits, self.length_len, 0, LENGTH_NUM)
                    self.length = Huff(self.length_len) if any(self.length_len) else None
                elif self.block_type == 3:
                    # Realign to 16 bits, then R0..R2 as little-endian 32-bit values.
                    if bits.n == 0:
                        pass
                    bits.ensure(16)
                    bits.n -= bits.n % 16
                    bits.buf &= (1 << bits.n) - 1
                    pos = bits.byte_pos()
                    bits.p = pos
                    bits.buf = 0
                    bits.n = 0
                    d = bits.d
                    self.R = [int.from_bytes(d[pos + 4 * k:pos + 4 * k + 4], "little") for k in range(3)]
                    bits.p = pos + 12
                else:
                    raise LzxError("bad block type %d" % self.block_type)
            this = min(self.block_remaining, target - len(out))
            if self.block_type == 3:
                d = bits.d
                out += d[bits.p:bits.p + this]
                bits.p += this
                self.block_remaining -= this
                continue
            end = len(out) + this
            while len(out) < end:
                sym = self.main.decode(bits)
                if sym < NUM_CHARS:
                    out.append(sym)
                    continue
                sym -= NUM_CHARS
                mlen = sym & 7
                if mlen == 7:
                    if self.length is None:
                        raise LzxError("no length tree")
                    mlen += self.length.decode(bits)
                mlen += MIN_MATCH
                slot = sym >> 3
                if slot > 2:
                    extra = EXTRA_BITS[slot]
                    if self.block_type == 2 and extra >= 3:
                        off = POSITION_BASE[slot] - 2 + (bits.read(extra - 3) << 3) + self.aligned.decode(bits)
                    elif self.block_type == 2 and extra > 0:
                        off = POSITION_BASE[slot] - 2 + bits.read(extra)
                    else:
                        off = POSITION_BASE[slot] - 2 + bits.read(extra)
                    self.R = [off, self.R[0], self.R[1]]
                elif slot == 0:
                    off = self.R[0]
                elif slot == 1:
                    off = self.R[1]
                    self.R[1] = self.R[0]
                    self.R[0] = off
                else:
                    off = self.R[2]
                    self.R[2] = self.R[0]
                    self.R[0] = off
                if off > len(out) or off <= 0:
                    raise LzxError("match before start (off %d at %d)" % (off, len(out)))
                for _ in range(mlen):
                    out.append(out[-off])
            self.block_remaining -= this
            # Overshoot (a match crossing the frame end) is kept, as LZX allows.
            if len(out) > end:
                self.block_remaining -= len(out) - end
        return bytes(out[start:target])

    def frame_end(self, bits):
        # After each 32 KiB frame the bitstream realigns to 16 bits.
        if bits.n % 16:
            bits.n -= bits.n % 16
            bits.buf &= (1 << bits.n) - 1
