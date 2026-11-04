#!/usr/bin/env python3
"""Add four private fan-made character outfits to an Ex-Box NXAK v2 kit.

The original kit remains untouched. This script reuses its rig and selected
garment meshes, dyes copies of their textures, and adds small original meshes
for hair ribbons, outfit details, and Cereza's plush companion.
"""
import argparse
import math
import struct
from pathlib import Path

import numpy as np


def pack(fmt, *values):
    return struct.pack(fmt, *values)


def name_bytes(name):
    encoded = name.encode("utf-8")
    if len(encoded) > 255:
        raise ValueError(name)
    return pack("<B", len(encoded)) + encoded


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read(self, size):
        result = self.data[self.pos:self.pos + size]
        if len(result) != size:
            raise ValueError("truncated NXAK")
        self.pos += size
        return result

    def unpack(self, fmt):
        return struct.unpack(fmt, self.read(struct.calcsize(fmt)))

    def string(self):
        size, = self.unpack("<B")
        return self.read(size).decode("utf-8", "replace")


def parse_kit(data):
    r = Reader(data)
    if r.read(4) != b"NXAK" or r.unpack("<I")[0] != 2:
        raise ValueError("expected NXAK version 2")
    joints, = r.unpack("<I")
    joint_names = []
    for _ in range(joints):
        joint_names.append(r.string())
        r.read(2)
    tex_count_at = r.pos
    textures, = r.unpack("<I")
    texture_data_at = r.pos
    tex = []
    for _ in range(textures):
        nm = r.string()
        w, h = r.unpack("<HH")
        ref = r.unpack("<3f")
        rgba = r.read(w * h * 4)
        tex.append((nm, w, h, ref, rgba))
    texture_end = r.pos
    bodies, = r.unpack("<I")
    rest = []
    for _ in range(bodies):
        r.string()
        matrices = r.read(joints * 48)
        rest.append([struct.unpack_from("<12f", matrices, j * 48) for j in range(joints)])
        r.read(8)
        clips, = r.unpack("<I")
        for _ in range(clips):
            r.string()
            frames, fps = r.unpack("<Hf")
            r.read(frames * joints * 48)
    part_count_at = r.pos
    part_count, = r.unpack("<I")
    part_data_at = r.pos
    parts = []
    for _ in range(part_count):
        nm = r.string()
        body, slot = r.unpack("<BB")
        ibm = r.read(joints * 48)
        nprim, = r.unpack("<I")
        prims = []
        for _ in range(nprim):
            tid, tint, flags, nverts = r.unpack("<hBBI")
            vb = r.read(nverts * 40)
            ntris, = r.unpack("<I")
            ix = r.read(ntris * 12)
            prims.append((tid, tint, flags, nverts, vb, ntris, ix))
        parts.append((nm, body, slot, ibm, prims))
    part_end = r.pos
    faces, = r.unpack("<I")
    for _ in range(faces):
        p, q, w, h = r.unpack("<HHHH")
        r.read(12 + 6)
        has = r.unpack("<5B")
        r.read(sum(has) * w * h * 4)
    morphs, = r.unpack("<I")
    for _ in range(morphs):
        r.string()
        p, n = r.unpack("<HI")
        r.read(n * 28)
    catalog_count_at = r.pos
    cat_count, = r.unpack("<I")
    categories = []
    for _ in range(cat_count):
        start = r.pos
        label = r.string()
        kind, zoom, arg, nitems = r.unpack("<BBBH")
        items = []
        for _ in range(nitems):
            item_label = r.string()
            body, = r.unpack("<B")
            item = {"label": item_label, "bodies": body, "parts": [], "hides": []}
            if kind == 0:
                bits, np = r.unpack("<IH")
                item["bits"] = bits
                item["parts"] = list(r.unpack("<" + "H" * np)) if np else []
                nh, = r.unpack("<H")
                r.read(nh * 2)
                hides, = r.unpack("<H")
                for _ in range(hides):
                    b, prim, nt = r.unpack("<BHI")
                    tris = list(r.unpack("<" + "I" * nt)) if nt else []
                    item["hides"].append((b, prim, tris))
            elif kind in (1, 2):
                r.read(2)
            elif kind == 3:
                r.read(3)
            items.append(item)
        categories.append((label, kind, data[start:r.pos], items))
    catalog_end_at = r.pos
    profiles = []
    if r.pos < len(data):
        if r.read(4) != b"NXVP":
            raise ValueError("unknown NXAK trailer")
        nprofiles, = r.unpack("<B")
        for _ in range(nprofiles):
            profile = r.unpack("<BHffB")
            choices = [r.unpack("<BH") for _ in range(profile[-1])]
            profiles.append((profile, choices))
    if r.pos != len(data):
        raise ValueError("trailing NXAK bytes")
    return locals()


def encode_texture(name, rgb):
    w = h = 8
    pixel = bytes((*rgb, 255))
    return name_bytes(name) + pack("<HH3f", w, h, 1.0, 1.0, 1.0) + pixel * (w * h)


def dye_texture(name, old, rgb):
    _, w, h, ref, rgba = old
    pixels = np.frombuffer(rgba, np.uint8).copy().reshape(h, w, 4)
    light = np.mean(pixels[:, :, :3].astype(np.float32), axis=2) / 255.0
    target = np.asarray(rgb, dtype=np.float32)
    pixels[:, :, :3] = np.clip(target[None, None, :] * (0.65 + 0.65 * light[:, :, None]), 0, 255).astype(np.uint8)
    return name_bytes(name) + pack("<HH3f", w, h, 1.0, 1.0, 1.0) + pixels.tobytes()


def suit_texture(name, base, accent):
    """A dark woven surface; the silhouette comes from the skinned body mesh."""
    w = h = 64
    yy, xx = np.indices((h, w))
    thread = (((xx + yy * 0.35) % 16) < 1.0).astype(np.float32)
    ripple = (np.sin(xx * 0.21) * np.sin(yy * 0.17) + 1.0) * 0.5
    rgb = np.asarray(base)[None, None, :] + thread[:, :, None] * np.asarray(accent)[None, None, :] * 0.17
    rgb = np.broadcast_to(rgb, (h, w, 3)).copy() + ripple[:, :, None] * 5
    rgba = np.concatenate((np.clip(rgb, 0, 255).astype(np.uint8),
                           np.full((h, w, 1), 255, np.uint8)), axis=2)
    return name_bytes(name) + pack("<HH3f", w, h, 1.0, 1.0, 1.0) + rgba.tobytes()


def encode_part(name, body, slot, ibm, prims):
    out = bytearray(name_bytes(name) + pack("<BB", body, slot) + ibm + pack("<I", len(prims)))
    for tid, tint, flags, nverts, vb, ntris, ix in prims:
        out += pack("<hBBI", tid, tint, flags, nverts) + vb + pack("<I", ntris) + ix
    return bytes(out)


def mesh_prim(vertices, triangles, texture, bone):
    vb = bytearray()
    for x, y, z in vertices:
        vb += pack("<8f8B", x, y, z, 0.0, 0.0, 1.0, 0.5, 0.5, bone, 0, 0, 0, 255, 0, 0, 0)
    ix = b"".join(pack("<3I", *tri) for tri in triangles)
    return texture, 0, 1, len(vertices), bytes(vb), len(triangles), ix


def box(cx, cy, cz, sx, sy, sz):
    v = [(cx + dx*sx/2, cy + dy*sy/2, cz + dz*sz/2)
         for dz in (-1, 1) for dy in (-1, 1) for dx in (-1, 1)]
    t = [(0,1,3),(0,3,2),(4,7,5),(4,6,7),(0,4,5),(0,5,1),
         (2,3,7),(2,7,6),(0,2,6),(0,6,4),(1,5,7),(1,7,3)]
    return v, t


def octa(cx, cy, cz, rx, ry, rz):
    v = [(cx,cy+ry,cz),(cx,cy-ry,cz),(cx-rx,cy,cz),(cx+rx,cy,cz),
         (cx,cy,cz-rz),(cx,cy,cz+rz)]
    t = [(0,2,4),(0,4,3),(0,3,5),(0,5,2),(1,4,2),(1,3,4),(1,5,3),(1,2,5)]
    return v, t


def ellipsoid(cx, cy, cz, rx, ry, rz, segments=12, rings=7):
    vertices = []
    for j in range(rings + 1):
        phi = math.pi * j / rings
        for i in range(segments):
            theta = 2 * math.pi * i / segments
            vertices.append((cx + rx * math.sin(phi) * math.cos(theta),
                             cy + ry * math.cos(phi),
                             cz + rz * math.sin(phi) * math.sin(theta)))
    triangles = []
    for j in range(rings):
        for i in range(segments):
            a = j * segments + i
            b = j * segments + (i + 1) % segments
            triangles.extend(((a, b, b + segments), (a, b + segments, a + segments)))
    return vertices, triangles


def tube(points, radius, sides=7):
    centers = [np.asarray(p, dtype=float) for p in points]
    vertices = []
    for i, c in enumerate(centers):
        tangent = centers[min(i+1, len(centers)-1)] - centers[max(0, i-1)]
        tangent /= max(np.linalg.norm(tangent), 1e-8)
        across = np.cross(tangent, [0.0, 0.0, 1.0])
        if np.linalg.norm(across) < 1e-5:
            across = np.cross(tangent, [0.0, 1.0, 0.0])
        across /= np.linalg.norm(across)
        depth = np.cross(tangent, across)
        local_radius = radius[i] if isinstance(radius, (list, tuple)) else radius
        for k in range(sides):
            a = 2 * math.pi * k / sides
            vertices.append(tuple(c + local_radius * (math.cos(a) * across + math.sin(a) * depth)))
    triangles = []
    for i in range(len(centers)-1):
        for k in range(sides):
            a = i*sides+k; b = i*sides+(k+1)%sides
            triangles.extend(((a,b,b+sides),(a,b+sides,a+sides)))
    return vertices, triangles


def fitted_shell(body_part, texture, lower=0.115, upper=1.39, lift=0.004, bare_shoulders=False):
    """Copy the avatar's own skinned surface, including joint weights.

    Triangle IDs are returned so the original skin can be hidden exactly
    beneath the garment. This is the core of the reusable fitting path.
    """
    _, _, _, ibm, prims = body_part
    tid, tint, flags, count, source_vb, ntris, source_ix = prims[0]
    vertices = bytearray(source_vb)
    y = []
    xcoords = []
    for i in range(count):
        offset = i * 40
        x, height, z, nx, ny, nz = struct.unpack_from("<6f", vertices, offset)
        y.append(height)
        xcoords.append(x)
        struct.pack_into("<3f", vertices, offset, x + nx*lift, height + ny*lift, z + nz*lift)
    triangles = np.frombuffer(source_ix, dtype="<u4").reshape(-1, 3)
    selected = []
    for i, tri in enumerate(triangles):
        if not all(lower <= y[int(v)] <= upper for v in tri):
            continue
        cx = sum(xcoords[int(v)] for v in tri) / 3
        cy = sum(y[int(v)] for v in tri) / 3
        if bare_shoulders and abs(cx) > .22 and 1.00 < cy < 1.42:
            continue
        selected.append(i)
    indices = triangles[selected].astype("<u4").tobytes()
    return ibm, (texture, 0, flags, count, bytes(vertices), len(selected), indices), selected


def body_surface_lines(body_part, paths, texture, width=.0045):
    """Lay line artwork onto the front of the skinned body, retaining weights."""
    _, _, _, ibm, prims = body_part
    source_vb = prims[0][4]
    count = prims[0][3]
    rows = np.frombuffer(source_vb, np.uint8).reshape(count, 40)
    xyz = np.frombuffer(source_vb, dtype="<f4").reshape(count, 10)[:, :3]
    front = xyz[:, 2] > .015
    cloud = xyz[front]
    weights = rows[front, 32:40]
    vertices, triangles = bytearray(), []
    for path in paths:
        start = len(vertices) // 40
        for x, y in path:
            nearest = np.argsort((cloud[:, 0]-x)**2 + (cloud[:, 1]-y)**2)[:8]
            d = (cloud[nearest, 0]-x)**2 + (cloud[nearest, 1]-y)**2
            blend = 1 / (d + 0.00005)
            blend /= blend.sum()
            z = float(np.dot(cloud[nearest, 2], blend)) + .012
            chosen = nearest[0]
            for dx in (-width, width):
                vertices += pack("<8f", x+dx, y, z, 0.0, 0.0, 1.0, 0.5, 0.5)
                vertices += weights[chosen].tobytes()
        for i in range(len(path)-1):
            a = start + i*2
            triangles.extend(((a, a+1, a+3), (a, a+3, a+2)))
    ix = b"".join(pack("<3I", *t) for t in triangles)
    return ibm, (texture, 0, 1, len(vertices)//40, bytes(vertices), len(triangles), ix)


def wear_item(label, parts, hides):
    bits = sum(1 << i for i in range(2, 12))
    out = bytearray(name_bytes(label) + pack("<BIH", 2, bits, len(parts)))
    out += b"".join(pack("<H", p) for p in parts)
    out += pack("<HH", 0, len(hides))
    for body, prim, tris in hides:
        out += pack("<BHI", body, prim, len(tris))
        out += b"".join(pack("<I", tri) for tri in tris)
    return bytes(out)


def add_expansion(source):
    kit = parse_kit(source)
    cat = {label: items for label, kind, raw, items in kit["categories"]}
    original_parts = kit["parts"]
    new_textures = []
    new_parts = []

    def solid(name, rgb):
        index = len(kit["tex"]) + len(new_textures)
        new_textures.append(encode_texture(name, rgb))
        return index

    def select(category, index):
        item = cat[category][index]
        if not item["bodies"] & 2:
            raise ValueError(f"{category} {index} does not fit the feminine body")
        return item

    def copy_cloth(part_index, label, rgb):
        name, body, slot, ibm, prims = original_parts[part_index]
        dyed = []
        for i, (tid, tint, flags, nv, vb, nt, ix) in enumerate(prims):
            if tid >= 0:
                new_tid = len(kit["tex"]) + len(new_textures)
                new_textures.append(dye_texture(f"{label} {i}", kit["tex"][tid], rgb))
                tid, tint = new_tid, 0
            dyed.append((tid, tint, flags, nv, vb, nt, ix))
        result = len(original_parts) + len(new_parts)
        new_parts.append(encode_part(f"{label} {name}", body, slot, ibm, dyed))
        return result

    def accessory(label, specs, slot=11):
        prims = []
        for shape, location, size, color, bone in specs:
            tex = solid(label + " " + str(len(prims)), color)
            if shape == "tube":
                verts, tris = tube(location, size)
            else:
                verts, tris = {"box": box, "octa": octa, "sphere": ellipsoid}[shape](*location, *size)
            prims.append(mesh_prim(verts, tris, tex, bone))
        result = len(original_parts) + len(new_parts)
        new_parts.append(encode_part(label, 1, slot, original_parts[0][3], prims))
        return result

    frame_specs = []
    for x in (-0.065, 0.065):
        oval = [(x + .047*math.cos(i*2*math.pi/12),
                 1.592 + .026*math.sin(i*2*math.pi/12), .207)
                for i in range(13)]
        frame_specs.append(("tube", oval, .0035, (24,19,25), 19))
    frame_specs.append(("tube", [(-.018,1.594,.210),(0,1.601,.213),(.018,1.594,.210)],
                        .003, (24,19,25), 19))
    frames_part = accessory("Black oval glasses frames", frame_specs, 10)

    shell_parts = {}
    shell_hides = {}
    for title, base, glint in (
        ("Bayonetta 1", (15, 14, 20), (65, 57, 71)),
        ("Bayonetta 2", (14, 20, 34), (57, 83, 122)),
        ("Bayonetta 3", (21, 17, 28), (81, 56, 100)),
    ):
        tex_id = len(kit["tex"]) + len(new_textures)
        new_textures.append(suit_texture(title + " woven suit", base, glint))
        ibm, prim, hidden = fitted_shell(original_parts[1], tex_id,
                                         bare_shoulders=(title == "Bayonetta 1"))
        part_id = len(original_parts) + len(new_parts)
        new_parts.append(encode_part(title + " fitted catsuit", 1, 6, ibm, [prim]))
        shell_parts[title] = part_id
        shell_hides[title] = (1, 0, hidden)

    filigree_paths = []
    for side in (-1, 1):
        filigree_paths.append([(side*(.095 + .055*(y-.17)/.73 + .008*math.sin((.91-y)*19)), y)
                               for y in np.linspace(.90, .17, 65)])
        filigree_paths.append([(side*(.078 + .048*(y-.17)/.73 - .008*math.sin((.91-y)*19)), y)
                               for y in np.linspace(.90, .17, 65)])
    filigree_tex = solid("Bayonetta silver suit scrollwork", (125, 130, 143))
    ibm, filigree_prim = body_surface_lines(original_parts[1], filigree_paths, filigree_tex)
    filigree_part = len(original_parts) + len(new_parts)
    new_parts.append(encode_part("Bayonetta 1 fitted scrollwork", 1, 11, ibm, [filigree_prim]))

    def line(points, color, bone=5, radius=.005):
        if bone not in (19, 41):
            bone = 5
        return ("tube", points, radius, color, bone)

    silver = (151, 153, 163)
    gold = (190, 148, 72)
    red = (172, 22, 45)
    blue = (34, 112, 184)
    purple = (133, 58, 157)
    # Coordinates follow the kit's female body. Fine details stay attached to
    # their nearest rig joint; the fitted suit itself retains full skinning.
    b1_details = [
        line([(-.12,1.32,.142),(-.08,1.21,.155),(0,1.08,.153),(.08,1.21,.155),(.12,1.32,.142)],silver),
        line([(-.18,1.26,.105),(-.10,1.12,.153),(-.05,1.01,.145),(0,.95,.145),(.05,1.01,.145),(.10,1.12,.153),(.18,1.26,.105)],silver),
        line([(-.13,1.10,.155),(-.04,1.17,.16),(.04,1.17,.16),(.13,1.10,.155)],silver),
        line([(.03,1.79,-.10),(.13,1.68,-.15),(.23,1.55,-.18),(.30,1.39,-.19),
              (.34,1.23,-.17),(.39,1.06,-.16),(.43,.88,-.14),(.43,.70,-.12),(.38,.54,-.10)],
             (17,14,21),19,[.04,.06,.075,.085,.09,.085,.07,.05,.008]),
        line([(.06,1.80,-.06),(.16,1.65,-.12),(.26,1.51,-.15),(.33,1.36,-.16),
              (.38,1.19,-.14),(.45,1.01,-.12),(.48,.84,-.10),(.46,.66,-.08)],
             (23,19,26),19,[.015,.025,.03,.035,.04,.03,.02,.003]),
        line([(.03,1.77,-.05),(.14,1.64,-.13),(.25,1.50,-.15),(.31,1.38,-.14)],red,19,.008),
        line([(.15,1.63,-.13),(.26,1.49,-.14),(.34,1.31,-.13),(.42,1.13,-.10),(.46,.95,-.08)],red,19,.006),
        ("octa",(0,1.00,.165),(.036,.045,.017),gold,5),
        ("octa",(-.19,.91,.13),(.025,.035,.012),gold,2),
        ("octa",(.19,.91,.13),(.025,.035,.012),gold,3),
        ("box",(-.10,1.75,.12),(.028,.16,.018),red,19),
        ("box",(.10,1.75,.12),(.028,.16,.018),red,19),
    ]
    b2_details = [
        line([(-.15,1.32,.13),(-.08,1.18,.16),(0,1.05,.16),(.08,1.18,.16),(.15,1.32,.13)],silver),
        line([(-.17,1.13,.13),(-.08,1.02,.16),(0,.94,.16),(.08,1.02,.16),(.17,1.13,.13)],blue),
        ("octa",(0,1.11,.17),(.035,.048,.018),blue,5),
    ]
    b3_details = [
        line([(-.16,1.32,.13),(-.08,1.20,.16),(0,1.07,.16),(.08,1.20,.16),(.16,1.32,.13)],gold),
        line([(-.16,1.14,.14),(0,.96,.17),(.16,1.14,.14)],purple),
        ("octa",(0,1.03,.17),(.043,.056,.02),purple,5),
        ("octa",(-.11,1.73,.15),(.055,.05,.02),purple,19),
        ("octa",(.11,1.73,.15),(.055,.05,.02),purple,19),
    ]

    variants = [
        ("Bayonetta 1", {"Hairstyle":77,"Shoes":23,"Gloves":6}, (18,17,23), b1_details),
        ("Bayonetta 2", {"Hairstyle":66,"Shoes":29}, (17,22,36), b2_details),
        ("Bayonetta 3", {"Hairstyle":80,"Shoes":23}, (22,18,29), b3_details),
        ("Young Cereza + Cheshire", {"Hairstyle":67,"Costume":12,"Shoes":18},
         (31,27,52), [
             ("octa",(0.10,1.70,0.19),(.085,.06,.028),(167,36,57),19),
             ("sphere",(0.59,1.12,0.03),(.105,.14,.085),(93,66,113),41),
             ("sphere",(0.59,1.27,0.03),(.09,.085,.075),(95,68,115),41),
             ("octa",(0.52,1.33,0.03),(.030,.055,.025),(111,68,129),41),
             ("octa",(0.66,1.33,0.03),(.030,.055,.025),(111,68,129),41),
             ("octa",(0.56,1.28,0.11),(.015,.018,.008),(232,210,105),41),
             ("octa",(0.62,1.28,0.11),(.015,.018,.008),(232,210,105),41),
             ("sphere",(0.59,1.22,0.105),(.037,.022,.014),(210,179,166),41),
             ("box",(0.59,1.20,0.11),(.08,.018,.010),(165,44,68),41),
             ("sphere",(0.59,1.13,0.117),(.025,.023,.01),(165,44,68),41),
             ("octa",(0,1.31,.14),(.04,.04,.012),(178,33,53),5)]),
    ]
    items = [name_bytes("None") + pack("<BIHHH", 3, 0, 0, 0, 0)]
    for title, selection, dye, specs in variants:
        parts = []
        hides = []
        if title in shell_parts:
            parts.append(shell_parts[title])
            hides.append(shell_hides[title])
        if title == "Bayonetta 1":
            parts.append(filigree_part)
        for category, index in selection.items():
            item = select(category, index)
            hides.extend(item["hides"])
            if category == "Glasses":
                parts.extend(item["parts"])
            else:
                for p in item["parts"]:
                    parts.append(copy_cloth(p, title, (20, 18, 23) if category == "Hairstyle" else dye))
        parts.append(frames_part)
        parts.append(accessory(title + " accents", specs))
        items.append(wear_item(title, parts, hides))
    new_category = name_bytes("Character Set") + pack("<BBBH", 0, 0, 5, len(items)) + b"".join(items)

    raw_cat = []
    labels = []
    for label, kind, raw, entries in kit["categories"]:
        if label == "Hairstyle":
            raw_cat.append(new_category)
            labels.append("Character Set")
        raw_cat.append(raw)
        labels.append(label)
    if len(raw_cat) != len(kit["categories"]) + 1:
        raise ValueError("Hairstyle category not found")
    ci = {label: i for i, label in enumerate(labels)}
    style_choices = [
        {"Body Type":1,"Skin Tone":1,"Eyes":1,"Eyebrows":23,"Eyebrow Colour":0,
         "Nose":9,"Mouth":23,"Lip Colour":6,"Chin":6,"Eye Shadow":5,
         "Eye Shadow Colour":4,"Hairstyle":77,"Hair Colour":0},
        {"Body Type":1,"Skin Tone":1,"Eyes":34,"Eyebrows":1,"Eyebrow Colour":0,
         "Mouth":5,"Lip Colour":5,"Eye Shadow":3,"Eye Shadow Colour":1,
         "Hairstyle":66,"Hair Colour":0},
        {"Body Type":1,"Skin Tone":1,"Eyes":25,"Eyebrows":23,"Eyebrow Colour":0,
         "Mouth":5,"Lip Colour":5,"Eye Shadow":5,"Eye Shadow Colour":0,
         "Hairstyle":80,"Hair Colour":0},
        {"Body Type":1,"Skin Tone":1,"Eyes":23,"Eyebrows":10,"Eyebrow Colour":0,
         "Nose":16,"Mouth":11,"Lip Colour":0,"Chin":8,"Face Paint":6,
         "Hairstyle":67,"Hair Colour":0},
    ]
    trailer = bytearray(b"NXVP" + pack("<B", 4))
    for index, choices in enumerate(style_choices, 1):
        body_scale, head_scale = (0.73, 1.18) if index == 4 else (1.0, 1.0)
        trailer += pack("<BHffB", ci["Character Set"], index, body_scale, head_scale, len(choices))
        for label, choice in choices.items():
            trailer += pack("<BH", ci[label], choice)
    result = (source[:kit["tex_count_at"]] + pack("<I", len(kit["tex"]) + len(new_textures))
              + source[kit["texture_data_at"]:kit["texture_end"]] + b"".join(new_textures)
              + source[kit["texture_end"]:kit["part_count_at"]]
              + pack("<I", len(original_parts) + len(new_parts))
              + source[kit["part_data_at"]:kit["part_end"]] + b"".join(new_parts)
              + source[kit["part_end"]:kit["catalog_count_at"]]
              + pack("<I", len(raw_cat)) + b"".join(raw_cat)
              + trailer)
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    source = args.source.read_bytes()
    result = add_expansion(source)
    parsed = parse_kit(result)
    assert len(parsed["categories"]) == 29
    assert [x[0] for x in parsed["categories"]].count("Character Set") == 1
    args.output.write_bytes(result)
    print(f"Wrote {args.output}: {len(result):,} bytes, {len(parsed['parts'])} parts, {len(parsed['tex'])} textures")


if __name__ == "__main__":
    main()
