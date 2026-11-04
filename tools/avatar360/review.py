"""Diagnostic PNG only; GLB is the deliverable for tools/render_avatar.py.

This is not the Ex-Box C++ renderer and does not certify its console output.
"""
import math
from pathlib import Path
from portable import inverse, mul, point, png_bytes, sample


def render(path, parts, xf, pose, width=480, height=720):
    skin = [mul(m, inverse(x['bind'])) for m, x in zip(pose, xf)]
    meshes, all_pos = [], []
    for p in parts:
        for q in p['prims']:
            verts = []
            for v in q['vertices']:
                pos, normal = [0., 0., 0.], [0., 0., 0.]
                for j, weight in zip(v['joints'], v['weights']):
                    if not weight: continue
                    a, b = point(skin[j], v['pos']), point(skin[j], v['normal'], True)
                    for k in range(3): pos[k] += weight*a[k]; normal[k] += weight*b[k]
                length = math.sqrt(sum(x*x for x in normal)) or 1.
                normal = [x/length for x in normal]
                key = sum(a*b for a, b in zip(normal, [-.45, .55, .70]))
                light = .45+.55*max(0., abs(key) if q['flags'] & 1 else key)
                uv = v['uvs'][q['layer']] if v['uvs'] else [0., 0.]
                verts.append((pos, light, uv)); all_pos.append(pos)
            meshes.append((q, verts))
    lo, hi = min(p[1] for p in all_pos), max(p[1] for p in all_pos)
    scale = min(height*.88/(hi-lo), width*.88/(max(p[0] for p in all_pos)-min(p[0] for p in all_pos)))
    center = (hi+lo)/2
    rgba, depth = bytearray(width*height*4), [-float('inf')]*(width*height)
    for y in range(height):
        shade = int(38+16*y/height)
        rgba[y*width*4:(y+1)*width*4] = bytes([shade, shade+5, shade+10, 255])*width
    for q, vs in meshes:
        ps = [(width/2+p[0]*scale, height/2-(p[1]-center)*scale, p[2], light, uv) for p, light, uv in vs]
        for at in range(0, len(q['indices']), 3):
            a, b, c = [ps[i] for i in q['indices'][at:at+3]]
            det = (b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1])
            if abs(det) < 1e-10: continue
            for y in range(max(0, math.floor(min(a[1], b[1], c[1]))), min(height, math.ceil(max(a[1], b[1], c[1])))):
                for x in range(max(0, math.floor(min(a[0], b[0], c[0]))), min(width, math.ceil(max(a[0], b[0], c[0])))):
                    wb = ((x+.5-a[0])*(c[1]-a[1])-(c[0]-a[0])*(y+.5-a[1]))/det
                    wc = ((b[0]-a[0])*(y+.5-a[1])-(x+.5-a[0])*(b[1]-a[1]))/det
                    wa = 1-wb-wc
                    if min(wa, wb, wc) < 0: continue
                    z = a[2]*wa+b[2]*wb+c[2]*wc; o = y*width+x
                    if z < depth[o]: continue
                    uv = [a[4][k]*wa+b[4][k]*wb+c[4][k]*wc for k in range(2)]
                    col = sample(q['image'], uv, 3)
                    alpha = col[3] if q['flags'] & 12 else 1.
                    if alpha < .5 and q['flags'] & 4: continue
                    if alpha <= 0: continue
                    light = a[3]*wa+b[3]*wb+c[3]*wc
                    for k in range(3):
                        value = min(255, max(0, round(col[k]*light*255)))
                        rgba[o*4+k] = round(value*alpha+rgba[o*4+k]*(1-alpha))
                    depth[o] = z
    Path(path).write_bytes(png_bytes(width, height, rgba))
