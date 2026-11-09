"""Small stdlib bridge for decoded reconstruction data. No runtime changes.

Material equations follow the existing tools/xavatar_bake.py; the optional
NumPy transform functions in xavatar.py are not loaded or called here.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import ast
import math
from pathlib import Path
import struct
import types
import zlib


def decoder():
    path = Path(__file__).resolve().parents[1] / 'xavatar.py'
    tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
    # Reuse the repository's bit/shape decoder; transforms below need no NumPy.
    omit = {'quat_mat', 'trs', 'joint_xforms', 'pose_world'}
    tree.body = [n for n in tree.body
                 if not (isinstance(n, ast.Import) and any(a.name == 'numpy' for a in n.names))
                 and not (isinstance(n, ast.FunctionDef) and n.name in omit)]
    mod = types.ModuleType('avatar360_decoder')
    exec(compile(tree, str(path), 'exec'), mod.__dict__)
    return mod


def identity():
    return [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.]


def mul(a, b):
    return [sum(a[r*4+k]*b[k*4+c] for k in range(3)) + (a[r*4+3] if c == 3 else 0)
            for r in range(3) for c in range(4)]


def inverse(a):
    x = [[a[r*4+c] for c in range(3)] + [float(r == c) for c in range(3)] for r in range(3)]
    for c in range(3):
        p = max(range(c, 3), key=lambda r: abs(x[r][c]))
        x[p], x[c] = x[c], x[p]
        v = x[c][c]
        if abs(v) < 1e-12:
            raise ValueError('Singular skeleton matrix')
        x[c] = [t/v for t in x[c]]
        for r in range(3):
            if r != c:
                k = x[r][c]
                x[r] = [u-k*v for u, v in zip(x[r], x[c])]
    return [v for r in range(3) for v in
            (x[r][3:] + [-sum(x[r][c+3]*a[c*4+3] for c in range(3))])]


def trs(t, q, s=(1., 1., 1.)):
    n = math.sqrt(sum(v*v for v in q)) or 1.
    x, y, z, w = [v/n for v in q]
    rot = [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w),
           2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w),
           2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]
    return [rot[r*3+c]*s[c] if c < 3 else t[r] for r in range(3) for c in range(4)]


def point(m, p, normal=False):
    return [sum(m[r*4+k]*p[k] for k in range(3)) + (0 if normal else m[r*4+3]) for r in range(3)]


def xforms(joints, scales=None):
    scales = scales or [[1., 1., 1.] for _ in joints]
    bind = [trs(j['pos'], j['rot']) for j in joints]
    out = []
    for i, j in enumerate(joints):
        p = j['parent'] if 0 <= j['parent'] < i else -1
        local = mul(inverse(bind[p]), bind[i]) if p >= 0 else bind[i]
        rest = mul(local, trs([0, 0, 0], [0, 0, 0, 1], scales[i]))
        if p >= 0:
            rest = mul(out[p]['rest'], rest)
        pws = [math.sqrt(sum(out[p]['rest'][r*4+c]**2 for r in range(3)))
               for c in range(3)] if p >= 0 else [1., 1., 1.]
        out.append(dict(bind=bind[i], rest=rest, parent=p, scale=scales[i], pws=pws,
                        local_t=[local[3], local[7], local[11]]))
    return out


def pose_world(xf, frame):
    out = []
    for i, x in enumerate(xf):
        if i < len(frame):
            t, q, s = frame[i]
            local = trs([(x['local_t'][k]+t[k])*x['pws'][k] for k in range(3)], q,
                        [s[k]*x['scale'][k] for k in range(3)])
            out.append(mul(out[x['parent']], local) if x['parent'] >= 0 else local)
        else:
            out.append(x['rest'])
    return out


def png_bytes(w, h, pixels):
    if len(pixels) != w*h*4:
        raise ValueError('RGBA byte count')
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))
    raw = b''.join(b'\0'+pixels[y*w*4:(y+1)*w*4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            +chunk(b'IDAT', zlib.compress(raw))+chunk(b'IEND', b''))


def read_png(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('PNG signature')
    pos, packed, info = 8, bytearray(), None
    while pos < len(data):
        n = struct.unpack_from('>I', data, pos)[0]
        kind, payload = data[pos+4:pos+8], data[pos+8:pos+8+n]
        if len(payload) != n or zlib.crc32(kind+payload) != struct.unpack_from('>I', data, pos+8+n)[0]:
            raise ValueError('PNG CRC')
        if kind == b'IHDR':
            info = struct.unpack('>IIBBBBB', payload)
        if kind == b'IDAT':
            packed.extend(payload)
        pos += n+12
        if kind == b'IEND':
            break
    if info is None or info[2:] not in ((8, 6, 0, 0, 0), (8, 2, 0, 0, 0)):
        raise ValueError('Only noninterlaced RGB/RGBA8 source PNGs supported')
    w, h = info[:2]
    channels = 4 if info[3] == 6 else 3
    stride = w*channels
    raw = zlib.decompress(packed)
    if len(raw) != h*(stride+1):
        raise ValueError('PNG dimensions')
    prev, output = bytearray(stride), bytearray()
    for y in range(h):
        f = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        if f > 4:
            raise ValueError('PNG filter')
        for x in range(stride):
            a, b, c = (row[x-channels] if x >= channels else 0), prev[x], (prev[x-channels] if x >= channels else 0)
            if f == 1: v = a
            elif f == 2: v = b
            elif f == 3: v = (a+b)//2
            elif f == 4:
                p = a+b-c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                v = a if pa <= pb and pa <= pc else b if pb <= pc else c
            else: v = 0
            row[x] = (row[x]+v) & 255
        if channels == 4:
            output.extend(row)
        else:
            for x in range(0, stride, 3): output.extend(row[x:x+3]+b'\xff')
        prev = row
    return w, h, bytes(output)


def sample(image, uv, flags, border=False):
    if image is None:
        return (0., 0., 0., 0.)
    w, h, data = image
    u, v = uv
    if border and (u < 0 or u > 1 or v < 0 or v > 1):
        return (0., 0., 0., 0.)
    if not flags & 1: u = min(1., max(0., u))
    if not flags & 2: v = min(1., max(0., v))
    fx, fy = u*w-.5, v*h-.5
    x, y = math.floor(fx), math.floor(fy)
    tx, ty = fx-x, fy-y
    x0, x1 = ((x % w, (x+1) % w) if flags & 1 else (max(0, min(w-1, x)), max(0, min(w-1, x+1))))
    y0, y1 = ((y % h, (y+1) % h) if flags & 2 else (max(0, min(h-1, y)), max(0, min(h-1, y+1))))
    a, b, c, d = (y0*w+x0)*4, (y0*w+x1)*4, (y1*w+x0)*4, (y1*w+x1)*4
    return tuple(((data[a+k]*(1-tx)+data[b+k]*tx)*(1-ty)+(data[c+k]*(1-tx)+data[d+k]*tx)*ty)/255 for k in range(4))


def material(batch, images, overrides=None, replacements=None):
    const = {p['usage']: p['values'] for p in batch['params'] if p['type'] in (2, 3)}
    const.update(overrides or {})
    params = {p['usage']: p for p in batch['params'] if p['type'] == 1}
    samplers = {}
    for usage, p in params.items():
        im = (replacements or {}).get(usage, images[p['index']] if p['index'] < len(images) else None)
        if im is not None:
            samplers[usage] = (im, min(p['uv_layer'], 5), p['flags'], 6 <= usage <= 12 and p['flags'] == 0)
    head = batch['shader_id'] == 4 and 5 in params
    base = 5 if head else 1
    out_layer = min(params[base]['uv_layer'], 5) if base in params else 0
    if out_layer >= batch.get('uv_count', 6): out_layer = 0
    def evaluate(uv):
        def get(k):
            if k not in samplers: return None
            im, layer, flags, border = samplers[k]
            return sample(im, uv[layer] if layer < len(uv) else uv[0], flags, border)
        if not head:
            col = get(1) or (1., 1., 1., 1.)
            rgb, weight = list(col[:3]), 0.
            intensity, decal = get(2), get(3)
            if intensity is not None:
                cs = [const.get(22+k, (1., 1., 1., 1.)) for k in range(3)]
                rec = [sum(cs[k][j]*intensity[k] for k in range(3)) for j in range(3)]
                rgb = [a+(b-a)*intensity[3] for a, b in zip(rgb, rec)]
                weight = intensity[3]*intensity[0]
            if decal is not None:
                rgb = [a+(b-a)*decal[3] for a, b in zip(rgb, decal)]
                weight *= 1-decal[3]
            return rgb+[col[3]], weight
        tint = [const.get(13+k, (1., 1., 1., 1.)) for k in range(9)]
        skin, rgb = tint[0][:3], list(tint[0][:3])
        b = get(5)
        if b:
            mixed = [tint[7][k]*b[0]+tint[8][k]*b[1]+skin[k]*b[2] for k in range(3)]
            rgb = [a+(v-a)*b[3] for a, v in zip(rgb, mixed)]
        s = get(11)
        if s: rgb = [a+(tint[5][k]*s[0]-a)*s[3] for k, a in enumerate(rgb)]
        for usage, k in ((12, 2), (9 if 9 in params else 10, 3), (6, 6), (7 if 7 in params else 8, 4)):
            f = get(usage)
            if f: rgb = [a+(tint[k][j]*f[0]+f[1]+skin[j]*f[2]-a)*f[3] for j, a in enumerate(rgb)]
        return rgb+[1.], 0.
    return out_layer, samplers, evaluate


def bake(batch, images, overrides=None, replacements=None, tint=False):
    layer, samplers, evaluate = material(batch, images, overrides, replacements)
    base = samplers.get(5 if batch['shader_id'] == 4 else 1)
    # Same output cap as build_avatar_kit360.py; original PNGs stay untouched.
    w, h = [min(256, max(32, n)) for n in base[0][:2]] if base else (32, 32)
    if batch['shader_id'] == 4: w = h = 256
    out, covered = bytearray(w*h*4), bytearray(w*h)
    alpha = batch['shader_id'] in (1, 3)
    verts, indices = batch['vertices'], batch['indices']
    def put(x, y, uv):
        col, weight = evaluate(uv)
        if not alpha and tint: col[3] = weight
        elif not alpha: col[3] = 0.
        o = (y*w+x)*4
        out[o:o+4] = bytes(min(255, max(0, int(c*255+.5))) for c in col)
        covered[y*w+x] = 1
    used = {v[1] for v in samplers.values()}
    same_uv = all(all(v['uvs'][k if k < len(v['uvs']) else 0] == v['uvs'][layer]
                      for k in used) for v in verts if v['uvs'])
    if same_uv:
        for y in range(h):
            for x in range(w):
                uv = [(x+.5)/w, (y+.5)/h]
                put(x, y, [uv]*6)
    else:
        for at in range(0, len(indices), 3):
            tri = [verts[i]['uvs'] for i in indices[at:at+3]]
            a, b, c = [[uv[layer][0]*w, uv[layer][1]*h] for uv in tri]
            det = (b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1])
            if abs(det) < 1e-12: continue
            x0, x1 = math.floor(min(a[0], b[0], c[0])), math.ceil(max(a[0], b[0], c[0]))
            y0, y1 = math.floor(min(a[1], b[1], c[1])), math.ceil(max(a[1], b[1], c[1]))
            # Some stock wristwear spans eleven repeats. Do not discard those
            # triangles as the older baker does; retain a hard corrupt-data cap.
            if x1-x0 > 64*w or y1-y0 > 64*h: raise ValueError('Unbounded UV triangle')
            e = -.6/max(math.sqrt(abs(det)), 1.)
            for y in range(y0, y1+1):
                for x in range(x0, x1+1):
                    wb = ((x+.5-a[0])*(c[1]-a[1])-(c[0]-a[0])*(y+.5-a[1]))/det
                    wc = ((b[0]-a[0])*(y+.5-a[1])-(x+.5-a[0])*(b[1]-a[1]))/det
                    wa = 1-wb-wc
                    if min(wa, wb, wc) < e: continue
                    uv = [[wa*tri[0][k][j]+wb*tri[1][k][j]+wc*tri[2][k][j] for j in range(2)] for k in range(len(tri[0]))]
                    put(x % w, y % h, uv)
        for _ in range(4):
            old, cov = out[:], covered[:]
            for y in range(h):
                for x in range(w):
                    o = y*w+x
                    if cov[o]: continue
                    ns = [((y+dy) % h)*w+(x+dx) % w for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
                    ns = [n for n in ns if cov[n]]
                    if ns:
                        out[o*4:o*4+4] = bytes(round(sum(old[n*4+k] for n in ns)/len(ns)) for k in range(4))
                        covered[o] = 1
    return layer, (w, h, bytes(out))
