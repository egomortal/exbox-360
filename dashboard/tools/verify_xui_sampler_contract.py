"""Trace original XAM sampler callbacks and execute their bounded integer leaves."""
from pathlib import Path
import argparse
import hashlib
import json
import random
import re
import struct
from audit_original_runtime import Image

MASK64 = (1 << 64) - 1


def sha(data):
    return hashlib.sha256(data).hexdigest()


def mask(bits, begin, end):
    return sum(1 << (bits - 1 - n) for n in range(bits)
               if (begin <= n <= end if begin <= end else n >= begin or n <= end))


def rotate(value, amount, bits):
    amount %= bits
    value &= (1 << bits) - 1
    return ((value << amount) | (value >> ((bits - amount) % bits))) & ((1 << bits) - 1)


class Leaf:
    def __init__(self, image, start, end):
        self.image = image
        self.instructions = list(image.cs.disasm(image.data[start-image.base:end-image.base], start))
        assert sum(x.size for x in self.instructions) == end-start
        assert self.instructions[-1].mnemonic == 'blr'

    def run(self, sampler, raw, state, anisotropy_index, dirty):
        base = 0x10000000
        memory = bytearray(0x6000)
        struct.pack_into('>6I', memory, 0x480 + sampler * 24, *state)
        memory[0x2edc + sampler] = anisotropy_index
        struct.pack_into('>Q', memory, 0x20, dirty)
        regs = [0] * 32
        regs[3:6] = [base, sampler, raw]

        def number(value):
            return regs[int(value[1:])] if value.startswith('r') else int(value, 0)

        def load(at, size):
            at &= 0xffffffff
            if base <= at <= base+len(memory)-size:
                return int.from_bytes(memory[at-base:at-base+size], 'big')
            if self.image.base <= at <= self.image.base+len(self.image.data)-size:
                return int.from_bytes(self.image.data[at-self.image.base:at-self.image.base+size], 'big')
            raise ValueError(f'Out-of-range load {at:x}')

        def store(at, size, value):
            at &= 0xffffffff
            if not base <= at <= base+len(memory)-size:
                raise ValueError(f'Out-of-range write {at:x}')
            memory[at-base:at-base+size] = (value & ((1 << (size*8))-1)).to_bytes(size, 'big')

        for ins in self.instructions:
            op = ins.mnemonic
            args = ins.op_str.split(', ')
            if op == 'blr':
                break
            if op in ('lwz', 'lbz', 'ld', 'stw', 'std'):
                address = re.fullmatch(r'(-?0x[0-9a-f]+|-?\d+)\((r\d+)\)', args[1])
                if not address:
                    raise ValueError(ins.op_str)
                at = number(address[2]) + int(address[1], 0)
                size = {'lbz': 1, 'lwz': 4, 'ld': 8, 'stw': 4, 'std': 8}[op]
                if op.startswith('st'):
                    store(at, size, number(args[0]))
                else:
                    regs[int(args[0][1:])] = load(at, size)
                continue
            if op in ('lwzx', 'stwx'):
                at = number(args[1]) + number(args[2])
                if op == 'stwx':
                    store(at, 4, number(args[0]))
                else:
                    regs[int(args[0][1:])] = load(at, 4)
                continue
            dest = int(args[0][1:])
            values = [number(v) for v in args[1:]]
            if op == 'li': result = values[0]
            elif op == 'lis': result = values[0] << 16
            elif op in ('addi', 'add'): result = values[0] + values[1]
            elif op == 'mulli': result = values[0] * values[1]
            elif op == 'or': result = values[0] | values[1]
            elif op == 'andc': result = values[0] & ~values[1]
            elif op == 'clrldi': result = values[0] & mask(64, values[1], 63)
            elif op == 'rldicr': result = rotate(values[0], values[1], 64) & mask(64, 0, values[2])
            elif op == 'srd': result = values[0] >> (values[1] & 127) if (values[1] & 127) < 64 else 0
            elif op == 'slwi': result = (values[0] << values[1]) & 0xffffffff
            elif op == 'srwi': result = (values[0] & 0xffffffff) >> values[1]
            elif op == 'rotlwi': result = rotate(values[0], values[1], 32)
            elif op in ('rlwinm', 'rlwimi'):
                selected = mask(32, values[2], values[3])
                result = rotate(values[0], values[1], 32) & selected
                if op == 'rlwimi': result |= regs[dest] & ~selected & 0xffffffff
            else:
                raise ValueError(f'Unsupported instruction {ins.address:x}: {op}')
            regs[dest] = result & MASK64
        return list(struct.unpack_from('>6I', memory, 0x480+sampler*24)), struct.unpack_from('>Q', memory, 0x20)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    image = Image(args.root, 'xam')
    assert image.base == 0x81870000
    assert sha(image.data) == '1733db65e4609b9567ddfc461933e957baac52a4783c0615133db31ab13407fd'
    args.out.mkdir(parents=True, exist_ok=True)
    original = args.out/'original-byte-slices'
    original.mkdir(exist_ok=True)
    evidence = []
    ranges = {
        'device-create': (0x818f0650, 0x818f06d8),
        'device-initialize': (0x818f9578, 0x818f9790),
        'device-state-table-install': (0x818f90a0, 0x818f91f0),
        'set-address-u': (0x818f4540, 0x818f4578),
        'set-address-v': (0x818f4590, 0x818f45c8),
        'set-mag-filter': (0x818f4060, 0x818f40e4),
        'set-min-filter': (0x818f3f50, 0x818f3fd4),
        'bind-texture-preserves-sampler': (0x818f21c8, 0x818f234c),
        'xui-sampler-cache': (0x819355d0, 0x81935658),
        'xui-linear-flags': (0x81937010, 0x81937180),
        'xui-linear-clamped': (0x81937180, 0x819372d0),
        'xui-radial-clamped': (0x819372d0, 0x81937430),
    }
    for name, (start, end) in ranges.items():
        data = image.data[start-image.base:end-image.base]
        binary = original/(name+'.ppc')
        binary.write_bytes(data)
        (args.out/(name+'.asm')).write_text(image.dump(start), encoding='utf-8')
        evidence.append({'name': name, 'start': hex(start), 'end_exclusive': hex(end),
                         'offset': start-image.base, 'bytes': len(data), 'sha256': sha(data),
                         'provenance': 'original_byte_slice', 'path': str(binary)})
    table_address = 0x81aa14d0
    data = image.data[table_address-image.base:table_address-image.base+20*12]
    (original/'sampler-state-triplets.bin').write_bytes(data)
    callbacks = []
    for index in range(20):
        getter, setter, default = struct.unpack_from('>III', data, index*12)
        callbacks.append({'slot': hex(0x1e4+index*4), 'getter': hex(getter),
                          'setter': hex(setter), 'initial_raw': default})
    assert [(int(callbacks[n]['slot'],16),int(callbacks[n]['setter'],16)) for n in (0,1,4,5)] == [
        (0x1e4,0x818f4540),(0x1e8,0x818f4590),(0x1f4,0x818f4060),(0x1f8,0x818f3f50)]
    tests = []
    rng = random.Random(1888)
    for name, word, shift, raw_values in [
            ('set-address-u',0,10,range(8)),('set-address-v',0,13,range(8)),
            ('set-mag-filter',3,19,range(4)),('set-min-filter',3,21,range(4))]:
        leaf = Leaf(image, *ranges[name])
        count = 0
        for sampler in range(26):
            for raw in raw_values:
                for repeat in range(16):
                    state = [rng.getrandbits(32) for _ in range(6)]
                    dirty = rng.getrandbits(64)
                    result, dirty_result = leaf.run(sampler, raw, state, repeat % 8, dirty)
                    bits = 3 if word == 0 else 2
                    target_mask = ((1 << bits)-1) << shift
                    assert ((result[word] >> shift) & ((1 << bits)-1)) == raw, (name,sampler,raw,state,result)
                    if word == 0:
                        assert result[word] & ~target_mask == state[word] & ~target_mask
                        assert result[1:] == state[1:]
                    else:
                        assert result[:3] == state[:3]
                        assert result[5] == state[5]
                        assert result[word] & ~(target_mask | (7 << 25)) == state[word] & ~(target_mask | (7 << 25))
                    assert dirty_result == dirty | (1 << (63-sampler))
                    count += 1
        tests.append({'callback': name,'cases': count,'samplers':26,'raw_values':list(raw_values),
                      'anisotropy_lookup_indices':list(range(8)) if word==3 else [],
                      'result':'pass'})
    assert mask(32,19,21) == 7 << 10
    assert mask(32,16,18) == 7 << 13
    assert mask(32,11,12) == 3 << 19
    assert mask(32,9,10) == 3 << 21
    addressing=image.data[0x81876bc8-image.base:0x81876bc8-image.base+16]
    address_table=struct.unpack('>4I',addressing)
    assert address_table==(0,1,2,3)
    (original/'xui-addressing-table.bin').write_bytes(addressing)
    flag_cases=[]
    for flags in range(64):
        u_index=rotate(flags,2,32)&mask(32,28,29)
        v_index=rotate(flags,0,32)&mask(32,28,29)
        min_filter=rotate(~flags,28,32)&1
        mag_filter=rotate(~flags,27,32)&1
        assert address_table[u_index//4]==flags&3
        assert address_table[v_index//4]==(flags>>2)&3
        assert min_filter==(~(flags>>4))&1
        assert mag_filter==(~(flags>>5))&1
        flag_cases.append(dict(flags=flags,u=address_table[u_index//4],v=address_table[v_index//4],
                               min_filter=min_filter,mag_filter=mag_filter))
    report = {
        'scope':'owner-supplied early XAM image only; later-era state-provider equivalence unverified',
        'source':{'path':str(args.root/'xam.pe'),'sha256':sha(image.data),'base':hex(image.base)},
        'callback_initialization':{'address':hex(0x818f90a0),'triplets_address':hex(table_address),
            'triplets_sha256':sha(data),'setter_slot_base':'0x1e4','triplets':callbacks},
        'verified': {'address_u': {'raw':2,'fetch_dword':0,'lsb_shift':10,'bits':3,'xenos':'kClampToEdge'},
            'address_v': {'raw':2,'fetch_dword':0,'lsb_shift':13,'bits':3,'xenos':'kClampToEdge'},
            'mag_filter': {'raw':1,'fetch_dword':3,'lsb_shift':19,'bits':2,'xenos':'kLinear'},
            'min_filter': {'raw':1,'fetch_dword':3,'lsb_shift':21,'bits':2,'xenos':'kLinear'}},
        'tests':tests,'executed_cases':sum(x['cases'] for x in tests),'slices':evidence,
        'flag_sensitive_linear':{'address':'0x81937010','address_table':'0x81876bc8',
            'table_sha256':sha(addressing),'table_values':address_table,'cases':flag_cases,
            'u':'flags & 3','v':'(flags >> 2) & 3',
            'min_filter':'(~(flags >> 4)) & 1','mag_filter':'(~(flags >> 5)) & 1'},
        'limits':['GPU command emission not fully instruction-executed by this tool',
                  'Real Xenos fixed-point interpolation and final pixel rounding unverified',
                  'Current ramp model not a complete emulator or proof of final lighting parity',
                  'Flag-sensitive linear helper may select other addressing/filtering; preserve its branches',
                  'Min/mag test scope raw values 0..3; anisotropy raw encodings above 3 not verified',
                  'No renderer, images, scenes, avatar allocator or console state modified']}
    (args.out/'SAMPLER_CONTRACT.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'executed_cases':report['executed_cases'],'result':'pass','out':str(args.out)}))


if __name__ == '__main__':
    main()
