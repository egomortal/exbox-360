"""Bounded static instruction evaluator for original XUI matrix leaf functions.

VMX128 fields and operation semantics are checked against the pinned Xenia
references in the evidence folder. Unknown instructions fail closed. This is
neither an Xbox host emulator nor an original GPU shader implementation.
"""
import argparse, collections, hashlib, json, math, struct
from pathlib import Path
import numpy as np
from audit_original_runtime import Image


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


class Leaf:
    def __init__(self, image):
        self.image = image
        self.r = [0] * 32
        self.f = [0.] * 32
        self.v = [bytes(16) for _ in range(128)]
        self.r[1] = 0x10000
        self.mem = {}
        self.counts = collections.Counter()
        self.ctr = 0

    def read(self, address, count):
        if self.image.base <= address < self.image.base + len(self.image.data):
            return self.image.data[address-self.image.base:address-self.image.base+count]
        return bytes(self.mem.get(address+i, 0) for i in range(count))

    def write(self, address, data):
        for i, byte in enumerate(data):
            self.mem[address+i] = byte

    def matrix(self, address, value=None):
        if value is not None:
            self.write(address, np.asarray(value, dtype='>f4').tobytes())
        return np.frombuffer(self.read(address, 64), dtype='>f4').astype(np.float64).reshape(4, 4)

    def run(self, address, stop=None):
        pc = address
        for _ in range(10000):
            if pc == stop:
                return
            word = self.image.u(pc-self.image.base)
            op = word >> 26
            d, a, b = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
            imm = signed(word & 0xffff, 16)
            ea = (0 if a == 0 else self.r[a]) + imm
            name = ''
            if word == 0x4e800020:
                return
            if op in (14, 15):
                name = 'addi' if op == 14 else 'addis'
                self.r[d] = ((0 if a == 0 else self.r[a]) + imm * (1 if op == 14 else 65536)) & 0xffffffff
            elif op in (32, 58):
                name = 'lwz' if op == 32 else 'ld'
                if op == 58:
                    ea = self.r[a] + signed(word & 0xfffc, 16)
                self.r[d] = int.from_bytes(self.read(ea, 4 if op == 32 else 8), 'big')
            elif op in (36, 62):
                name = 'stw' if op == 36 else 'std'
                count = 4 if op == 36 else 8
                if op == 62:
                    ea = self.r[a] + signed(word & 0xfffc, 16)
                self.write(ea, int(self.r[d] & ((1 << (count*8))-1)).to_bytes(count, 'big'))
            elif op in (48, 52):
                name = 'lfs' if op == 48 else 'stfs'
                if op == 48:
                    self.f[d] = struct.unpack('>f', self.read(ea, 4))[0]
                else:
                    self.write(ea, struct.pack('>f', self.f[d]))
            elif op == 16 and (word >> 21) & 31 == 16:
                name = 'bdnz'
                self.ctr -= 1
                if self.ctr:
                    pc += signed(word & 0xfffc, 16)
                    self.counts[name] += 1
                    continue
            elif op == 31 and ((word >> 1) & 1023) == 467:
                name = 'mtctr'
                self.ctr = self.r[d]
            elif op == 31 and ((word >> 1) & 1023) == 444:
                name = 'or'
                self.r[a] = self.r[d] | self.r[b]
            elif op == 31 and word & 0x7ff in (0x40e, 0x44e, 0x1ce, 0x50e, 0x54e):
                kind = word & 0x7ff
                address = (0 if a == 0 else self.r[a]) + self.r[b]
                offset = address & 15
                name = {0x40e:'lvlx', 0x44e:'lvrx', 0x1ce:'stvx', 0x50e:'stvlx', 0x54e:'stvrx'}[kind]
                if kind == 0x40e:
                    self.v[d] = self.read(address, 16-offset) + bytes(offset)
                elif kind == 0x44e:
                    self.v[d] = bytes(16-offset) + self.read(address-offset, offset)
                elif kind == 0x50e:
                    self.write(address, self.v[d][:16-offset])
                elif kind == 0x54e:
                    self.write(address-offset, self.v[d][16-offset:])
                else:
                    self.write(address & ~15, self.v[d])
            elif op == 4 and word & 0x7ff in (0x484, 0x08c, 0x18c):
                code = word & 0x7ff
                av, bv = self.v[a], self.v[b]
                name = {0x484:'vor', 0x08c:'vmrghw', 0x18c:'vmrglw'}[code]
                if code == 0x484:
                    self.v[d] = bytes(x | y for x, y in zip(av, bv))
                else:
                    start = 0 if code == 0x08c else 8
                    self.v[d] = b''.join(av[i:i+4]+bv[i:i+4] for i in (start, start+4))
            elif op == 4 and word & 0x7ff == 0x38c:
                name = 'vspltisw'
                self.v[d] = struct.pack('>i', signed(a, 5)) * 4
            elif op == 6 and word & 0x730 == 0x710:
                name = 'vrlimi128'
                vd = d | (((word >> 2) & 3) << 5)
                vb = b | ((word & 3) << 5)
                rotation = (word >> 6) & 3
                src = self.v[vb]
                src = src[rotation*4:] + src[:rotation*4]
                dst = bytearray(self.v[vd])
                for k in range(4):
                    if a & (8 >> k):
                        dst[k*4:k*4+4] = src[k*4:k*4+4]
                self.v[vd] = bytes(dst)
            elif op == 6 and word & 0x630 == 0x210:
                name = 'vpermwi128'
                vd = d | (((word >> 2) & 3) << 5)
                vb = b | ((word & 3) << 5)
                perm = a | (((word >> 6) & 7) << 5)
                src = self.v[vb]
                self.v[vd] = b''.join(src[((perm >> shift) & 3)*4:((perm >> shift) & 3)*4+4] for shift in (6,4,2,0))
            elif op == 6 and word & 0x7f0 == 0x730:
                name = 'vspltw128'
                vd = d | (((word >> 2) & 3) << 5)
                vb = b | ((word & 3) << 5)
                self.v[vd] = self.v[vb][(a & 3)*4:(a & 3)*4+4] * 4
            elif op == 6 and word & 0x7f0 == 0x7f0:
                name = 'vupkd3d128 zero short2'
                vd = d | (((word >> 2) & 3) << 5)
                vb = b | ((word & 3) << 5)
                if a >> 2 != 1 or self.v[vb] != bytes(16):
                    raise ValueError('Only zero NORMSHORT2 constant is supported')
                self.v[vd] = struct.pack('>4f', 0, 0, 0, 1)
            elif op == 5 and word & 0x3d0 == 0x1d0:
                name = 'vmsum4fp128'
                vd = d | (((word >> 2) & 3) << 5)
                va = a | (((word >> 5) & 1) << 5) | (((word >> 10) & 1) << 6)
                vb = b | ((word & 3) << 5)
                left, right = (np.frombuffer(self.v[k], dtype='>f4') for k in (va, vb))
                self.v[vd] = struct.pack('>f', float(np.dot(left, right))) * 4
            elif op == 5 and word & 0x3d0 == 0x350:
                name = 'vsel128'
                vd = d | (((word >> 2) & 3) << 5)
                va = a | (((word >> 5) & 1) << 5) | (((word >> 10) & 1) << 6)
                vb = b | ((word & 3) << 5)
                self.v[vd] = bytes((x & ~mask) | (y & mask) for x,y,mask in zip(self.v[va], self.v[vb], self.v[vd]))
            elif op == 63 and (word >> 1) & 1023 == 40:
                name = 'fneg'
                self.f[d] = -self.f[b]
            elif op == 4 and word & 0x7f3 in (0x0c3, 0x1c3):
                name = 'lvx128' if word & 0x7f3 == 0x0c3 else 'stvx128'
                vd = d | (((word >> 2) & 3) << 5)
                address = ((0 if a == 0 else self.r[a]) + self.r[b]) & ~15
                if name == 'lvx128':
                    self.v[vd] = self.read(address, 16)
                else:
                    self.write(address, self.v[vd])
            else:
                raise ValueError(f'Unsupported instruction {pc:08x}: {word:08x}')
            self.counts[name] += 1
            pc += 4
        raise ValueError('Instruction limit reached')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    image = Image(args.root, 'xam')
    counts = collections.Counter()
    checks = []
    for address, name, values, expected in [
        (0x8193e288, 'scale', (2., -3., 4.), np.diag([2., -3., 4., 1.])),
        (0x8193e390, 'translation', (2., -3., 4.), np.array([[1,0,0,0],[0,1,0,0],[0,0,1,0],[2,-3,4,1.]]))]:
        machine = Leaf(image)
        machine.r[3] = 0x20000
        machine.f[1:4] = values
        machine.run(address)
        actual = machine.matrix(0x20000)
        np.testing.assert_allclose(actual, expected)
        counts.update(machine.counts)
        checks.append(dict(name=name, actual=actual.tolist()))
    rng = np.random.default_rng(1888)
    for index in range(32):
        left = rng.normal(size=(4,4)).astype(np.float32)
        right = rng.normal(size=(4,4)).astype(np.float32)
        machine = Leaf(image)
        machine.r[3:6] = [0x20000, 0x20100, 0x20200]
        machine.matrix(0x20100, left)
        machine.matrix(0x20200, right)
        machine.run(0x8193e4b0)
        np.testing.assert_allclose(machine.matrix(0x20000), left @ right, atol=2e-6)
        counts.update(machine.counts)
        machine.r[3:5] = [0x20300, 0x20100]
        machine.run(0x8193eb70)
        np.testing.assert_array_equal(machine.matrix(0x20300), left.T)
    checks.append(dict(name='matrix multiply and brush storage transpose', random_cases=32))
    for angle in (0., .4, -1.2):
        machine = Leaf(image)
        machine.r[31] = 0x20000
        cs, sn = math.cos(angle), math.sin(angle)
        machine.write(machine.r[1]+0x50, struct.pack('>2f', cs, sn))
        machine.run(0x8193ecd4, 0x8193ed58)
        expected = [[cs,sn,0,0],[-sn,cs,0,0],[0,0,1,0],[0,0,0,1]]
        np.testing.assert_allclose(machine.matrix(0x20000), expected, atol=1e-7)
    checks.append(dict(name='rotation matrix body with supplied sin/cos', angles_radians=[0,.4,-1.2],
                       scope='Original matrix assembly body; scalar sin/cos implementation not evaluated'))
    report = dict(image_sha256=hashlib.sha256(image.data).hexdigest(), checks=checks,
                  instruction_counts=dict(counts), matrix_multiply='output = left @ right; row-vector convention',
                  brush_storage='transpose, not inverse',
                  limitations=['Only listed matrix leaf functions evaluated; no Xbox host services or GPU shaders executed.'])
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
