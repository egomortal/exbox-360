"""NXAK v2 writer and GLB review export; only stdlib, all art stays private."""
import json
import math
from pathlib import Path
import struct
from portable import inverse, mul, png_bytes, point

SCALE = 1.2
VERTEX = struct.Struct('<8f8B')


def quantize(weights):
    total = sum(weights)
    if total <= 0: raise ValueError('Vertex without skin weights')
    q = [int(w/total*255+.5) for w in weights]
    q[max(range(4), key=lambda k: q[k])] += 255-sum(q)
    if min(q) < 0 or max(q) > 255: raise ValueError('Invalid skin weights')
    return q


def matrix_bytes(m):
    return struct.pack('<12f', *(v*SCALE if i % 4 == 3 else v for i, v in enumerate(m)))


def write_kit(path, joints, names, xf, parts, clips, label, morphs=None):
    """A bounded, standalone kit. Cross-item merge metadata lives in its sidecar.

    Developer catalog text is an Ex-Box addition with no 360 original.
    Source item labels are preserved verbatim (within NXAK's 255-byte bound).
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    textures = [q['image'] for p in parts for q in p['prims']]
    if len(textures) > 8192 or len(parts) > 8192 or len(joints) > 255:
        raise ValueError('NXAK count limit')
    def string(f, s):
        b = s.encode('utf-8')[:255]
        while True:
            try: b.decode('utf-8'); break
            except UnicodeDecodeError: b = b[:-1]
        f.write(bytes([len(b)])+b)
    mesh_bytes = 0
    with path.open('wb') as f:
        f.write(b'NXAK'+struct.pack('<II', 2, len(joints)))
        for n, x in zip(names, xf): string(f, n); f.write(struct.pack('<h', x['parent']))
        f.write(struct.pack('<I', len(textures)))
        for i, (w, h, data) in enumerate(textures):
            string(f, str(i)); f.write(struct.pack('<HH3f', w, h, 1., 1., 1.)); f.write(data)
        f.write(struct.pack('<I', 1))
        string(f, label)
        for x in xf: f.write(matrix_bytes(x['rest']))
        f.write(struct.pack('<ffI', 1.36*SCALE, 1.55*SCALE, len(clips)))
        for name, fps, frames in clips:
            if not 0 < len(frames) <= 65535 or not 0 < fps <= 240: raise ValueError('Clip bounds')
            string(f, name); f.write(struct.pack('<Hf', len(frames), fps))
            for frame in frames:
                if len(frame) != len(joints): raise ValueError('Clip rig mismatch')
                for m in frame: f.write(matrix_bytes(m))
        f.write(struct.pack('<I', len(parts)))
        tex = 0
        for p in parts:
            string(f, p['name']); f.write(struct.pack('<BB', 255, 11))
            for x in xf: f.write(matrix_bytes(inverse(x['bind'])))
            f.write(struct.pack('<I', len(p['prims'])))
            for q in p['prims']:
                vs, ix = q['vertices'], q['indices']
                if len(ix) % 3 or any(i >= len(vs) or i < 0 for i in ix): raise ValueError('Triangle indices')
                f.write(struct.pack('<hBBI', tex, q.get('tint', 0), q['flags'], len(vs))); tex += 1
                for v in vs:
                    wq = quantize(v['weights'])
                    js = [j if w else 0 for j, w in zip(v['joints'], wq)]
                    if any(j < 0 or j >= len(joints) for j in js): raise ValueError('Joint index')
                    uv = v['uvs'][q['layer']] if v['uvs'] else (0., 0.)
                    vals = [x*SCALE for x in v['pos']]+list(v['normal'])+list(uv)
                    if not all(math.isfinite(x) for x in vals): raise ValueError('Nonfinite vertex')
                    f.write(VERTEX.pack(*vals, *js, *wq))
                f.write(struct.pack('<I', len(ix)//3)); f.write(struct.pack('<%dI' % len(ix), *ix))
                mesh_bytes += len(vs)*40+len(ix)*4
        f.write(struct.pack('<II', 0, 0))  # Face/morphs are baked for this selection.
        f.write(struct.pack('<I', 3))
        string(f, 'Converted selection'); f.write(struct.pack('<BBBH', 0, 0, 11, 1))
        string(f, label); f.write(struct.pack('<BIH', 3, 0, len(parts)))
        f.write(struct.pack('<%dH' % len(parts), *range(len(parts)))); f.write(struct.pack('<HH', 0, 0))
        for slot, name in ((0, 'Skin Tone'), (1, 'Hair Colour')):
            string(f, name); f.write(struct.pack('<BBBH', 3, 1, slot, 1))
            string(f, 'Source bake'); f.write(bytes([3, 255, 255, 255]))
    poses = sum(len(frames)*len(joints)*48 for _, _, frames in clips)
    return dict(file_bytes=path.stat().st_size, texture_bytes=sum(len(t[2]) for t in textures),
                mesh_bytes=mesh_bytes, pose_bytes=poses, joints=len(joints),
                textures=len(textures), parts=len(parts), clips=len(clips),
                triangles=sum(len(q['indices'])//3 for p in parts for q in p['prims']))


def export_glb(path, parts, xf, pose):
    """A posed, textured review scene. NXAK retains the animated rig separately.

    A static GLB is accepted by render_avatar.py (which then uses its nod fallback).
    No claim that the GLB's static pose reproduces the original facial animation.
    """
    blob = bytearray()
    doc = dict(asset=dict(version='2.0', generator='Ex-Box AV1 private converter'),
               scene=0, scenes=[dict(nodes=[])], nodes=[], meshes=[], materials=[],
               textures=[], images=[], samplers=[dict(magFilter=9729, minFilter=9729, wrapS=10497, wrapT=10497)],
               bufferViews=[], accessors=[])
    def view(data):
        while len(blob) % 4: blob.append(0)
        i = len(doc['bufferViews']); doc['bufferViews'].append(dict(buffer=0, byteOffset=len(blob), byteLength=len(data)))
        blob.extend(data); return i
    def accessor(rows, kind, component=5126):
        flat = [v for row in rows for v in row] if kind != 'SCALAR' else rows
        fmt = 'f' if component == 5126 else 'I'
        bv = view(struct.pack('<%d%s' % (len(flat), fmt), *flat))
        a = dict(bufferView=bv, componentType=component, count=len(rows), type=kind)
        if kind == 'VEC3': a.update(min=[min(r[k] for r in rows) for k in range(3)], max=[max(r[k] for r in rows) for k in range(3)])
        i = len(doc['accessors']); doc['accessors'].append(a); return i
    skin = [mul(m, inverse(x['bind'])) for m, x in zip(pose, xf)]
    for p in parts:
        prims = []
        for q in p['prims']:
            if not q['indices']: continue
            ps, ns, uvs = [], [], []
            for v in q['vertices']:
                pos, nrm = [0., 0., 0.], [0., 0., 0.]
                for j, w in zip(v['joints'], v['weights']):
                    if not w: continue
                    vp, vn = point(skin[j], v['pos']), point(skin[j], v['normal'], True)
                    for k in range(3): pos[k] += w*vp[k]*SCALE; nrm[k] += w*vn[k]
                n = math.sqrt(sum(v*v for v in nrm)) or 1.
                ps.append(pos); ns.append([v/n for v in nrm]); uvs.append(v['uvs'][q['layer']] if v['uvs'] else [0., 0.])
            w, h, pixels = q['image']; pixels = bytearray(pixels)
            if not q['flags'] & 12: pixels[3::4] = b'\xff'*(w*h)
            ti = len(doc['images']); doc['images'].append(dict(bufferView=view(png_bytes(w, h, pixels)), mimeType='image/png'))
            doc['textures'].append(dict(source=ti, sampler=0))
            mat = dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=ti), metallicFactor=0., roughnessFactor=.85),
                       doubleSided=bool(q['flags'] & 1))
            if q['flags'] & 4: mat.update(alphaMode='MASK', alphaCutoff=.5)
            elif q['flags'] & 8: mat.update(alphaMode='BLEND')
            doc['materials'].append(mat)
            prims.append(dict(attributes=dict(POSITION=accessor(ps, 'VEC3'), NORMAL=accessor(ns, 'VEC3'), TEXCOORD_0=accessor(uvs, 'VEC2')),
                              indices=accessor(q['indices'], 'SCALAR', 5125), material=ti))
        if prims:
            i = len(doc['nodes']); doc['scenes'][0]['nodes'].append(i)
            doc['nodes'].append(dict(name=p['name'], mesh=len(doc['meshes'])))
            doc['meshes'].append(dict(primitives=prims))
    doc['buffers'] = [dict(byteLength=len(blob))]
    jb = json.dumps(doc, separators=(',', ':')).encode()
    jb += b' '*((-len(jb)) % 4); blob.extend(b'\0'*((-len(blob)) % 4))
    Path(path).write_bytes(struct.pack('<III', 0x46546c67, 2, 28+len(jb)+len(blob))
                           +struct.pack('<I4s', len(jb), b'JSON')+jb+struct.pack('<I4s', len(blob), b'BIN\0')+blob)
