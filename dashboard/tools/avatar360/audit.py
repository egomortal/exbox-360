"""Independent structural parser and receipt auditor for AV1 outputs."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def inspect_kit(data):
    pos = 0
    arrays = []
    def take(n):
        nonlocal pos
        if n < 0 or pos+n > len(data): raise ValueError('Truncated NXAK')
        v = data[pos:pos+n]; pos += n; return v
    def unpack(fmt): return struct.unpack('<'+fmt, take(struct.calcsize('<'+fmt)))
    def number(fmt): return unpack(fmt)[0]
    def string(): return take(number('B')).decode('utf-8')
    def matrices(n):
        values = unpack(str(n*12)+'f')
        if not all(math.isfinite(v) for v in values): raise ValueError('Nonfinite matrix')
    if take(4) != b'NXAK' or number('I') != 2: raise ValueError('NXAK v2 signature')
    nj = number('I')
    if not 1 <= nj <= 255: raise ValueError('Joint count')
    for i in range(nj):
        string(); parent = number('h')
        if not -1 <= parent < i: raise ValueError('Joint hierarchy')
    nt = number('I')
    if nt > 8192: raise ValueError('Texture count')
    for _ in range(nt):
        string(); w, h, *ref = unpack('HH3f')
        if not w or not h or not all(math.isfinite(x) for x in ref): raise ValueError('Texture header')
        n = w*h*4; take(n); arrays.append(('texture', n))
    nb = number('I')
    if not 1 <= nb <= 8: raise ValueError('Body count')
    clips = []
    for _ in range(nb):
        string(); matrices(nj); _, _, nc = unpack('ffI')
        if nc > 256: raise ValueError('Clip count')
        for _ in range(nc):
            name = string(); frames, fps = unpack('Hf')
            if not frames or not 0 < fps <= 240: raise ValueError('Clip timing')
            matrices(frames*nj); arrays.append(('pose', frames*nj*48)); clips.append(name)
    nparts = number('I')
    if nparts > 8192: raise ValueError('Part count')
    mesh_bytes, triangles = 0, 0
    for _ in range(nparts):
        string(); body, slot = unpack('BB')
        if body != 255 and body >= nb: raise ValueError('Part body')
        if slot > 11: raise ValueError('Part slot')
        matrices(nj); npr = number('I')
        if npr > 64: raise ValueError('Primitive count')
        for _ in range(npr):
            tex, tint, flags, nv = unpack('hBBI')
            if not -1 <= tex < nt or tint > 4: raise ValueError('Primitive material')
            for _ in range(nv):
                v = unpack('8f8B')
                if not all(math.isfinite(x) for x in v[:8]): raise ValueError('Nonfinite vertex')
                if max(v[8:12]) >= nj or sum(v[12:]) != 255: raise ValueError('Skin weights/joints')
            ntri = number('I'); ix = unpack(str(ntri*3)+'I')
            if any(i >= nv for i in ix): raise ValueError('Index outside vertex array')
            if flags & 2: take(ntri)
            mesh_bytes += nv*40+ntri*12; triangles += ntri
    # Portable selections deliberately bake face maps and morphs; refuse surprises.
    if number('I') or number('I'): raise ValueError('Unexpected unbaked face/morph data')
    nc = number('I')
    if nc > 64: raise ValueError('Catalog count')
    for _ in range(nc):
        string(); kind, _, arg, count = unpack('BBBH')
        if kind not in (0, 1, 2, 3, 4): raise ValueError('Catalog kind')
        for _ in range(count):
            string(); mask = number('B')
            if not mask & 3: raise ValueError('Empty body mask')
            if kind == 0:
                number('I')
                for _ in range(2):
                    ids = unpack(str(number('H'))+'H')
                    if any(i >= nparts for i in ids): raise ValueError('Catalog part index')
                for _ in range(number('H')):
                    body, prim, n = unpack('BHI'); take(n*4)
                    if body >= nb: raise ValueError('Hide body')
            elif kind in (1, 2): number('h')
            elif kind == 3: take(3)
    if pos != len(data): raise ValueError('Trailing NXAK bytes')
    direct = sum(((n+32+16383)//16384)*16384 for _, n in arrays if n+32 >= 256*1024)
    heap = sum(n+32 for _, n in arrays if n+32 < 256*1024)+mesh_bytes
    return dict(joints=nj, textures=nt, bodies=nb, parts=nparts, triangles=triangles, clips=clips,
                file_bytes=len(data), texture_bytes=sum(n for k, n in arrays if k == 'texture'),
                pose_bytes=sum(n for k, n in arrays if k == 'pose'), mesh_bytes=mesh_bytes,
                estimated_direct_array_bytes=direct, minimum_heap_array_mesh_bytes=heap,
                note='Excludes vector objects, matrices outside clips, renderer copies, mips, SDL, and load overlap.')


def audit(source, output, verify_sources=True):
    source, output = Path(source).resolve(), Path(output).resolve()
    receipts = sorted((output/'items').glob('*/receipt.json'))
    sources, inventory, kits, records = {}, {}, {}, []
    for path in receipts:
        d = json.loads(path.read_text(encoding='utf-8')); records.append(d['record'])
        for rel, info in d['sources'].items():
            if rel in sources and sources[rel] != info: raise ValueError('Inconsistent source receipts')
            sources[rel] = info
        for rel, info in d['outputs'].items():
            p = (path.parent/rel).resolve()
            if not p.is_relative_to(output): raise ValueError('Output receipt escape')
            data = p.read_bytes()
            if hashlib.sha256(data).hexdigest() != info['sha256'] or len(data) != info['bytes']:
                raise ValueError('Output hash mismatch: '+str(p))
            key = p.relative_to(output).as_posix(); inventory[key] = info
            if p.suffix == '.nxak': kits[key] = inspect_kit(data)
        inventory[path.relative_to(output).as_posix()] = dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(), bytes=path.stat().st_size)
    template = json.loads((output/'review/template.json').read_text(encoding='utf-8'))
    sources.update(template['sources'])
    core = json.loads((output/'core/manifest.json').read_text(encoding='utf-8'))
    for body in core['bodies'].values(): sources.update(body['sources'])
    catalog_receipt = output/'catalog-provenance.json'
    if catalog_receipt.exists(): sources.update(json.loads(catalog_receipt.read_text(encoding='utf-8'))['sources'])
    for p in output.rglob('*'):
        if not p.is_file() or p.name in ('audit.json', 'files.json'): continue
        key = p.relative_to(output).as_posix()
        if key not in inventory:
            data = p.read_bytes(); inventory[key] = dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))
            if p.suffix == '.nxak': kits[key] = inspect_kit(data)
    if verify_sources:
        for rel, info in sources.items():
            p = (source/rel).resolve()
            if not p.is_relative_to(source): raise ValueError('Source receipt escape')
            data = p.read_bytes()
            if hashlib.sha256(data).hexdigest() != info['sha256'] or len(data) != info['bytes']:
                raise ValueError('Source changed: '+str(p))
    sizes = sorted(v['file_bytes'] for k, v in kits.items() if k.startswith('items/'))
    stats = dict(records=len(records), kits=len(kits), source_files_verified=len(sources) if verify_sources else 0,
                 output_files=len(inventory), output_bytes=sum(x['bytes'] for x in inventory.values()),
                 shard_file_bytes=dict(min=min(sizes, default=0), median=sizes[len(sizes)//2] if sizes else 0,
                                       p95=sizes[min(len(sizes)-1, int(len(sizes)*.95))] if sizes else 0,
                                       max=max(sizes, default=0)), kits_by_path=kits)
    (output/'audit.json').write_text(json.dumps(stats, indent=2)+'\n', encoding='utf-8')
    (output/'files.json').write_text(json.dumps(inventory, indent=2)+'\n', encoding='utf-8')
    return stats


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source'); parser.add_argument('output')
    args = parser.parse_args()
    result = audit(args.source, args.output)
    print(json.dumps({k: v for k, v in result.items() if k != 'kits_by_path'}, indent=2))
