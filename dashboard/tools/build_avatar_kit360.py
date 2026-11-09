#!/usr/bin/env python3
"""Builds a PRIVATE avatar kit (kit360.nxak) from the Xbox 360 avatar items in
your own console's system update, so the Ex-Box avatar editor can dress the
real 360 avatar on your PS5. The output is Microsoft's art: it stays in
private/ and on your own console (private_art/avatar/), never in a release.

    python3 tools/build_avatar_kit360.py ITEMS_DIR SKELETON OUT [--jobs N]

ITEMS_DIR  what tools/avatar_unpack.py wrote (with index.tsv)
SKELETON   the NXE avatar skeleton (538 bytes; the same rig Xenia embeds)
OUT        e.g. private/avatar/kit360.nxak

Decoding follows avatar-aura (MIT), a port of Xenia's xam/avatars (BSD).
Format: NXAK version 2 = version 1 (see build_avatar_kit.py) plus, after the
parts, these sections (little-endian, strings u8 length + bytes):
  faces:   u32 n; per head primitive: u16 part, u16 prim, u16 W, u16 H,
           i16 tex[6] (defaults: base, shadow, mouth, eye, facial hair, brow),
           u8 flags[6] (sampler: 1 wrap U, 2 wrap V, 4 clear outside 0..1),
           u8 has[5]; per channel with has: W*H pairs of i16 (UV * 8192;
           -32768 = none), the channel's UV at each base texel
  morphs:  u32 n; per morph: str name, u16 part, u32 count; per vertex:
           u16 prim, u16 vertex, f32 pos[3], f32 nrm[3]
  catalog: u32 n; per category: str label, u8 kind, u8 zoom, u8 arg, u16 items;
           per item: str label, u8 bodies (bit 0 masculine, bit 1 feminine), then
             kind 0 wear:   u32 bits, u16 n, u16 part[n], u16 m, u16 hat_part[m],
                            u16 h; per hide: u8 body, u16 prim, u32 k, u32 tri[k]
             kind 1 face:   i16 texture (-1: the head's own); arg = channel
                            (0 base, 1 shadow, 2 mouth, 3 eye, 4 facial hair, 5 brow)
             kind 2 morph:  i16 morph (-1: none)
             kind 3 colour: u8 r, g, b; arg = tint slot (0 skin, 1 hair, 2 lips,
                            3 eyes, 4 brows, 5 eye shadow, 6 facial hair)
             kind 4 body:   nothing (the item is the body index)
Primitive flags add: 4 alpha cut-out, 8 see-through (alpha blended),
16 the face (drawn with the head composite).
SPDX-License-Identifier: GPL-3.0-or-later (this script; the art is Microsoft's)
"""
import csv
import multiprocessing
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xavatar  # noqa: E402
import xavatar_bake as xb  # noqa: E402

S = 1.2  # metres: the 360 avatar is ~1.5 m to the top of its big head; ours stand ~1.8 m
FPS_KEEP = 20.0
WHITE = (1.0, 1.0, 1.0, 1.0)
TINT_NONE, TINT_SKIN, TINT_HAIR = 0, 1, 2
SLOT = {"body": 0, "eyes": 1, "hair": 3, "top": 5, "legs": 7, "feet": 8, "hat": 9, "glasses": 10, "extra": 11}
CAT_BITS = {"head": 1 << 0, "body": 1 << 1, "hair": 1 << 2, "shirt": 1 << 3, "trousers": 1 << 4,
            "shoes": 1 << 5, "hat": 1 << 6, "gloves": 1 << 7, "glasses": 1 << 8, "wrist": 1 << 9,
            "earrings": 1 << 10, "ring": 1 << 11}

# (label, pack category, slot, zoom, offer "None")
WEAR = [
    ("Hairstyle", "hair", "hair", True, False),
    ("Hat", "hat", "hat", True, True),
    ("Glasses", "glasses", "glasses", True, True),
    ("Earrings", "earrings", "extra", True, True),
    ("Costume", "costume", "top", False, True),
    ("Top", "shirt", "top", False, False),
    ("Bottoms", "trousers", "legs", False, False),
    ("Shoes", "shoes", "feet", False, False),
    ("Gloves", "gloves", "extra", False, True),
    ("Wristwear", "wrist", "extra", False, True),
    ("Ring", "ring", "extra", False, True),
]
# Clips: kit name, then pack names to try (masculine / feminine).
CLIPS = [
    ("idle", ["Animation Generic Stand 0"]),
    ("idle2", ["Animation Generic Stand 1"]),
    ("idle3", ["Animation Generic Stand 2"]),
    ("wave", ["Animation Generic Wave"]),
    ("talk", ["Animation {B} Laugh"]),
    ("dance", ["Animation Generic Celebration"]),
    ("interact", ["Animation Generic Clap"]),
    ("jump", ["Animation {B} Shocked Surprised"]),
    ("yawn", ["Animation {B} Yawn"]),
    ("look", ["Animation {B} Idle Looks Around"]),
    ("shift", ["Animation {B} Idle Shifts Weight"]),
    ("sad", ["Animation {B} Sad Cry"]),
    ("angry", ["Animation {B} Angry"]),
    ("confused", ["Animation {B} Confused"]),
]
# Our own swatches (plain colours).
PALETTES = {
    "Skin Tone": (0, [(255, 226, 200), (246, 208, 178), (234, 190, 154), (222, 172, 134), (204, 152, 112),
                      (180, 128, 92), (152, 104, 72), (122, 82, 58), (94, 62, 44), (68, 46, 34)]),
    "Hair Colour": (1, [(24, 20, 18), (56, 38, 26), (88, 58, 36), (122, 84, 52), (160, 118, 72), (204, 168, 108),
                        (232, 210, 152), (156, 62, 32), (196, 92, 44), (128, 128, 132), (220, 220, 224),
                        (44, 76, 168), (176, 52, 124), (64, 150, 92), (120, 60, 170)]),
    "Lip Colour": (2, [(196, 120, 110), (176, 92, 86), (150, 74, 70), (214, 140, 140), (204, 96, 120),
                       (170, 48, 58), (120, 60, 64), (226, 168, 150)]),
    "Eye Colour": (3, [(92, 60, 36), (56, 38, 26), (66, 110, 170), (44, 74, 130), (70, 132, 92),
                       (120, 110, 70), (110, 116, 126), (148, 92, 46), (96, 150, 170)]),
    "Eyebrow Colour": (4, [(24, 20, 18), (56, 38, 26), (88, 58, 36), (122, 84, 52), (172, 132, 84),
                           (156, 62, 32), (128, 128, 132), (220, 220, 224)]),
    "Eye Shadow Colour": (5, [(120, 90, 140), (90, 110, 160), (70, 130, 110), (150, 110, 90), (60, 60, 70),
                              (190, 120, 150), (200, 170, 100)]),
    "Facial Hair Colour": (6, [(24, 20, 18), (56, 38, 26), (88, 58, 36), (122, 84, 52), (172, 132, 84),
                               (156, 62, 32), (128, 128, 132), (220, 220, 224)]),
}
PALETTE_NAMES = {
    "Skin Tone": ["Porcelain", "Fair", "Light", "Light Medium", "Medium", "Olive", "Tan", "Brown", "Dark", "Deep"],
    "Hair Colour": ["Black", "Dark Brown", "Brown", "Light Brown", "Dark Blonde", "Blonde", "Platinum", "Auburn",
                    "Copper", "Grey", "White", "Blue", "Magenta", "Green", "Purple"],
    "Lip Colour": ["Natural", "Rose", "Berry", "Pink", "Bright Pink", "Red", "Plum", "Nude"],
    "Eye Colour": ["Brown", "Dark Brown", "Blue", "Deep Blue", "Green", "Hazel", "Grey", "Amber", "Aqua"],
    "Eyebrow Colour": ["Black", "Dark Brown", "Brown", "Light Brown", "Blonde", "Auburn", "Grey", "White"],
    "Eye Shadow Colour": ["Violet", "Blue", "Green", "Bronze", "Smoky", "Pink", "Gold"],
    "Facial Hair Colour": ["Black", "Dark Brown", "Brown", "Light Brown", "Blonde", "Auburn", "Grey", "White"],
}
CHANNELS = [(11, "shadow"), (12, "mouth"), (None, "eye"), (6, "fhair"), (None, "brow")]


def p2(x):
    return 1 << (max(int(x), 1) - 1).bit_length()


def color_table(blob):
    """An item's custom colour table (Xenia's ApplyItemColorTable): ARGB words."""
    if len(blob) < 4 + 24:
        return None
    le = int.from_bytes(blob[:4], "little") <= 0xFFFF
    cols = [int.from_bytes(blob[4 + i * 8:8 + i * 8], "little" if le else "big") for i in range(3)]
    if not any(cols):
        return None
    return [(((c >> 16) & 255) / 255, ((c >> 8) & 255) / 255, (c & 255) / 255, 1.0) for c in cols]


def bake_job(args):
    """One model: bakes each batch (separate process). Returns prims."""
    path, cat, colors_path = args
    m = xavatar.read_model(open(path, "rb").read())
    ims = [xb.image(t) for t in m["textures"]]
    ov = {22: WHITE}
    if colors_path and os.path.exists(colors_path):
        tab = color_table(open(colors_path, "rb").read())
        if tab:
            for k, c in enumerate(tab):
                ov[22 + k] = c
    prims = []
    for b in m["batches"]:
        if not b["vertices"] or not b["triangle_count"]:
            continue
        r = xb.recipe(b, ims, ov, None, False)
        src = r["color"]
        if src is not None:
            h, w = src.img.shape[:2]
            size = (min(256, max(32, p2(w))), min(256, max(32, p2(h))))
        else:
            size = (32, 32)
        img, wgt, cov = xb.bake(b, r, size)
        alpha_mode = {1: 4, 3: 8}.get(b["shader_id"], 0)
        prims.append({"batch": b, "out": r["out"], "img": img, "wgt": wgt, "alpha": alpha_mode})
    return path, prims, m


class Kit:
    def __init__(self):
        self.textures = []  # (name, w, h, ref, rgba bytes)
        self.parts = []     # dicts
        self.morphs = []
        self.faces = []
        self.catalog = []

    def add_texture(self, name, rgba, ref=(1.0, 1.0, 1.0)):
        h, w = rgba.shape[:2]
        self.textures.append((name, w, h, ref, (np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes()))
        return len(self.textures) - 1


def prim_from_batch(b, out_layer, tex, tint, flags):
    V = b["vertices"]
    P = np.array([v["pos"] for v in V], np.float32) * S
    N = np.array([v["normal"] for v in V], np.float32)
    UV = np.array([v["uvs"][out_layer] if v["uvs"] else (0.0, 0.0) for v in V], np.float32)
    J = np.array([v["joints"] for v in V], np.uint8)
    W = np.array([v["weights"] for v in V], np.float32)
    W = W / np.maximum(W.sum(1, keepdims=True), 1e-6)
    Wq = np.round(W * 255).astype(np.int32)
    Wq[:, 0] += 255 - Wq.sum(1)
    Wq = np.clip(Wq, 0, 255).astype(np.uint8)
    J = np.where(Wq > 0, J, 0).astype(np.uint8)
    I = np.array(b["indices"], np.uint32).reshape(-1, 3)
    return {"tex": tex, "tint": tint, "flags": flags, "P": P, "N": N, "UV": UV, "J": J, "W": Wq, "I": I}


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    jobs = os.cpu_count() or 4
    if "--jobs" in sys.argv:
        jobs = int(sys.argv[sys.argv.index("--jobs") + 1])
        a = [x for x in a if x != str(jobs)]
    if len(a) < 3:
        raise SystemExit(__doc__)
    items, skel_path, out_path = a[:3]
    rows = list(csv.reader(open(os.path.join(items, "index.tsv"), encoding="utf-8"), delimiter="\t"))[1:]
    by_index = {int(r[0]): r for r in rows}

    def files(r):
        """index row -> {block: path}."""
        cat, name = r[1], r[7] or r[6]
        safe = "".join("_" if ch in '\\/:*?"<>|' else ch for ch in name)[:80]
        base = os.path.join(items, cat, "%04d %s" % (int(r[0]), safe))
        return {k: base + ".%s.bin" % k for k in r[5].split(",") if k != "-"}

    def id0(r):
        h = r[4]
        return int(h[0:8], 16), int(h[8:12], 16)

    joints = xavatar.read_skeleton(open(skel_path, "rb").read())
    xf = xavatar.joint_xforms(joints)
    nj = len(joints)
    names = xavatar.JOINT_NAMES if nj == 71 else ["joint_%d" % i for i in range(nj)]

    def scaled(m):
        m = np.array(m, float)
        m[:3, 3] *= S
        return m

    ibm = [np.linalg.inv(scaled(x["bind"])) for x in xf]
    kit = Kit()

    # --- what to bake: bodies, the head, every wearable -----------------
    body_rows = [r for r in rows if r[1] == "body"]
    head_row = [r for r in rows if r[1] == "head"][0]
    wear_rows = {cat: [r for r in rows if r[1] == cat and "model" in r[5]] for _, cat, _, _, _ in WEAR}
    todo = []
    for r in body_rows:
        todo.append((files(r)["model"], "body", None))
    for cat, rs in wear_rows.items():
        for r in rs:
            f = files(r)
            todo.append((f["model"], cat, f.get("colors")))
    print("baking %d models with %d jobs..." % (len(todo), jobs), flush=True)
    with multiprocessing.Pool(jobs) as pool:
        baked = {}
        for i, (path, prims, model) in enumerate(pool.imap_unordered(bake_job, todo, chunksize=2)):
            baked[path] = (prims, model)
            if (i + 1) % 50 == 0:
                print("  %d/%d" % (i + 1, len(todo)), flush=True)

    def add_part(name, body, slot, path, tint):
        prims, model = baked[path]
        out = []
        for k, p in enumerate(prims):
            rgba = p["img"].copy()
            if tint != TINT_NONE:
                if p["alpha"]:
                    pass  # alpha stays the opacity; tinting then applies everywhere
                else:
                    rgba[..., 3] = p["wgt"]
            elif not p["alpha"]:
                rgba[..., 3] = 0
            tex = kit.add_texture("%s#%d" % (name, k), rgba)
            two = 1 if slot in ("hair", "hat") or p["alpha"] else 0
            out.append(prim_from_batch(p["batch"], p["out"], tex, tint, two | p["alpha"]))
        kit.parts.append({"name": name, "body": body, "slot": SLOT[slot], "prims": out, "model": model})
        return len(kit.parts) - 1

    body_part = {}
    for bi, r in enumerate(body_rows):
        body_part[bi] = add_part(r[7].lower(), bi, "body", files(r)["model"], TINT_SKIN)

    # --- the head: two primitives drawn with the face composite --------
    head_model = xavatar.read_model(open(files(head_row)["model"], "rb").read())
    head_ims = [xb.image(t) for t in head_model["textures"]]
    head_tex = [kit.add_texture("head#%d" % i, im) if im is not None else -1 for i, im in enumerate(head_ims)]
    head_prims = []
    face_entries = []
    for k, b in enumerate(head_model["batches"]):
        tex = {p["usage"]: p for p in b["params"] if p["type"] == 1}
        out_l = tex[5]["uv_layer"]
        head_prims.append(prim_from_batch(b, out_l, -1, TINT_NONE, 16))
        W = H = 256
        uvs, cov = xb.uv_map(b, out_l, (W, H))
        use = {"base": 5, "shadow": 11, "mouth": 12, "eye": 9 if 9 in tex else 10, "fhair": 6,
               "brow": 7 if 7 in tex else 8}
        order = ["base", "shadow", "mouth", "eye", "fhair", "brow"]
        texs, flags, maps = [], [], []
        for ch in order:
            p = tex.get(use[ch])
            if p is None:
                texs.append(-1)
                flags.append(0)
                maps.append(None)
                continue
            texs.append(head_tex[p["index"]])
            f = p["flags"] & 3
            if 6 <= use[ch] <= 12 and p["flags"] == 0:
                f |= 4
            flags.append(f)
            if ch == "base":
                continue
            m = uvs[:, :, min(p["uv_layer"], 5), :]
            q = np.clip(np.round(m * 8192), -32767, 32767).astype(np.int16)
            q[~cov] = -32768
            maps.append(q)
        face_entries.append({"prim": k, "W": W, "H": H, "tex": texs, "flags": flags, "maps": maps[1:] if len(maps) == 6 else maps})
    kit.parts.append({"name": "head", "body": 255, "slot": SLOT["eyes"], "prims": head_prims, "model": head_model})
    head_part = len(kit.parts) - 1
    for e in face_entries:
        e["part"] = head_part
        kit.faces.append(e)

    # --- face shapes (nose, chin, ears) ---------------------------------
    morph_cats = {}
    for cat in ("nose", "chin", "ears"):
        lst = []
        for r in rows:
            if r[1] != cat or "shape" not in r[5]:
                continue
            sh = xavatar.read_shape(open(files(r)["shape"], "rb").read())
            if not xavatar.shape_fits(sh, head_model):
                continue
            _, moves = xavatar.shape_targets(sh, head_model)
            verts = [(j, vi, tuple(np.array(v["pos"]) * S), v["normal"]) for j, vi, v in moves]
            kit.morphs.append({"name": r[7], "part": head_part, "verts": verts})
            lst.append((r[7], len(kit.morphs) - 1))
        morph_cats[cat] = lst

    # --- wearables ---------------------------------------------------------
    body_models = {bi: baked[files(r)["model"]][1] for bi, r in enumerate(body_rows)}

    def hides_for(r):
        a0, b0 = id0(r)
        out = []
        if a0 != 0x01000000 or b0 not in by_index:
            return out
        t = by_index[b0]
        f = files(t)
        if "shape" not in f:
            return out
        sh = xavatar.read_shape(open(f["shape"], "rb").read())
        for bi, bm in body_models.items():
            if xavatar.shape_fits(sh, bm):
                hide, _ = xavatar.shape_targets(sh, bm)
                per = {}
                for j, t_ in hide:
                    per.setdefault(j, []).append(t_)
                for j, ts in per.items():
                    out.append((bi, j, sorted(set(ts))))
        return out

    for label, cat, slot, zoom, none in WEAR:
        entries = []
        if none:
            entries.append({"label": "None", "bodies": 3, "bits": 0, "parts": [], "hat": [], "hides": []})
        for r in wear_rows[cat]:
            name = r[7] or r[6]
            if cat == "hair" and "(Hat)" in name:
                continue
            bodies = int(r[3]) & 3 or 3
            tint = TINT_HAIR if cat == "hair" else TINT_NONE
            pi = add_part("%s:%s" % (cat, name), 255, slot, files(r)["model"], tint)
            hat = []
            if cat == "hair":
                _, comp = id0(r)
                c = by_index.get(comp)
                if c is not None and c[1] == "hair" and "model" in c[5]:
                    todo_path = files(c)["model"]
                    if todo_path not in baked:
                        baked[todo_path] = bake_job((todo_path, "hair", None))[1:]
                    hat = [add_part("hair:%s" % (c[7] or c[6]), 255, slot, todo_path, TINT_HAIR)]
            bits = int(r[2], 16)
            entries.append({"label": name, "bodies": bodies, "bits": bits, "parts": [pi], "hat": hat,
                          "hides": hides_for(r)})
        kit.catalog.append({"label": label, "kind": 0, "zoom": zoom, "arg": SLOT[slot], "items": entries})

    # --- face features, shapes, colours, body type --------------------
    def feature_cat(label, cat, channel, none_label=None):
        items = []
        if none_label:
            items.append({"label": none_label, "bodies": 3, "tex": -1})
        for r in rows:
            if r[1] != cat or "texture" not in r[5]:
                continue
            s = xavatar.Bits(open(files(r)["texture"], "rb").read())
            im = xb.image(xavatar.read_texture(s))
            if im is None:
                continue
            items.append({"label": r[7] or r[6], "bodies": int(r[3]) & 3 or 3,
                          "tex": kit.add_texture("%s:%s" % (cat, r[7]), im)})
        return {"label": label, "kind": 1, "zoom": True, "arg": channel, "items": items}

    def colour_cat(label):
        slot, cols = PALETTES[label]
        return {"label": label, "kind": 3, "zoom": True, "arg": slot,
                "items": [{"label": n, "bodies": 3, "rgb": c} for n, c in zip(PALETTE_NAMES[label], cols)]}

    def morph_cat(label, cat):
        items = [{"label": "Default", "bodies": 3, "morph": -1}]
        items += [{"label": n, "bodies": 3, "morph": i} for n, i in morph_cats[cat]]
        return {"label": label, "kind": 2, "zoom": True, "arg": 0, "items": items}

    head_cats = [
        {"label": "Body Type", "kind": 4, "zoom": False, "arg": 0,
         "items": [{"label": "Masculine", "bodies": 3}, {"label": "Feminine", "bodies": 3}]},
        colour_cat("Skin Tone"),
        feature_cat("Eyes", "eyes", 3),
        colour_cat("Eye Colour"),
        feature_cat("Eyebrows", "eyebrows", 5),
        colour_cat("Eyebrow Colour"),
        morph_cat("Nose", "nose"),
        feature_cat("Mouth", "mouth", 2),
        colour_cat("Lip Colour"),
        morph_cat("Chin", "chin"),
        morph_cat("Ears", "ears"),
        feature_cat("Facial Hair", "facial_hair", 4, "None"),
        colour_cat("Facial Hair Colour"),
        feature_cat("Eye Shadow", "eyeshadow", 1, "None"),
        colour_cat("Eye Shadow Colour"),
        feature_cat("Face Paint", "facial_other", 0, "None"),
    ]
    wear_cats = kit.catalog
    hair_i = [i for i, c in enumerate(wear_cats) if c["label"] == "Hairstyle"][0]
    kit.catalog = head_cats + [wear_cats[hair_i], colour_cat("Hair Colour")] + \
        [c for i, c in enumerate(wear_cats) if i != hair_i]

    # --- bodies and clips ---------------------------------------------
    anim_rows = {r[7]: r for r in rows if r[1] == "animation" and "animation" in r[5]}
    head_top = max(v["pos"][1] for b in head_model["batches"] for v in b["vertices"]) * S
    bodies = []
    for bi, r in enumerate(body_rows):
        word = "Male" if bi == 0 else "Female"
        clips = []
        for kname, cands in CLIPS:
            for c in cands:
                n = c.replace("{B}", word)
                if n in anim_rows:
                    an = xavatar.read_animation(open(files(anim_rows[n])["animation"], "rb").read())
                    frames = [[scaled(m) for m in xavatar.pose_world(xf, fr)] for fr in an["pose"]]
                    clips.append((kname, an["fps"], frames))
                    break
        rest = [scaled(x["rest"]) for x in xf]
        bodies.append({"name": r[7].lower(), "rest": rest, "brow_y": 1.36 * S, "top_y": head_top, "clips": clips})
        print("body %s: %d clips" % (r[7], len(clips)), flush=True)

    write(out_path, names, [x["parent"] for x in xf], kit, bodies, ibm)


def w_str(f, s):
    b = s.encode("utf-8")[:255]
    f.write(struct.pack("<B", len(b)) + b)


def m34(m):
    return np.asarray(m, np.float32)[:3, :4].reshape(-1).tobytes()


def write(path, names, parents, kit, bodies, ibm):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"NXAK" + struct.pack("<I", 2))
        f.write(struct.pack("<I", len(names)))
        for n, p in zip(names, parents):
            w_str(f, n)
            f.write(struct.pack("<h", p))
        f.write(struct.pack("<I", len(kit.textures)))
        for name, w, h, ref, data in kit.textures:
            w_str(f, name[:255])
            f.write(struct.pack("<HH3f", w, h, *ref))
            f.write(data)
        f.write(struct.pack("<I", len(bodies)))
        for b in bodies:
            w_str(f, b["name"])
            f.write(b"".join(m34(m) for m in b["rest"]))
            f.write(struct.pack("<ffI", b["brow_y"], b["top_y"], len(b["clips"])))
            for name, fps, frames in b["clips"]:
                w_str(f, name)
                f.write(struct.pack("<Hf", len(frames), fps))
                for fr in frames:
                    f.write(b"".join(m34(m) for m in fr))
        f.write(struct.pack("<I", len(kit.parts)))
        for p in kit.parts:
            w_str(f, p["name"][:255])
            f.write(struct.pack("<BB", p["body"], p["slot"]))
            f.write(b"".join(m34(m) for m in ibm))
            f.write(struct.pack("<I", len(p["prims"])))
            for q in p["prims"]:
                f.write(struct.pack("<hBBI", q["tex"], q["tint"], q["flags"], len(q["P"])))
                vt = np.zeros(len(q["P"]), dtype=[("p", "<f4", 3), ("n", "<f4", 3), ("uv", "<f4", 2),
                                                  ("j", "u1", 4), ("w", "u1", 4)])
                vt["p"], vt["n"], vt["uv"], vt["j"], vt["w"] = q["P"], q["N"], q["UV"], q["J"], q["W"]
                f.write(vt.tobytes())
                f.write(struct.pack("<I", len(q["I"])))
                f.write(q["I"].astype("<u4").tobytes())
        # v2 sections
        f.write(struct.pack("<I", len(kit.faces)))
        for e in kit.faces:
            f.write(struct.pack("<HHHH", e["part"], e["prim"], e["W"], e["H"]))
            f.write(struct.pack("<6h", *e["tex"]))
            f.write(struct.pack("<6B", *e["flags"]))
            has = [m is not None for m in e["maps"]]
            f.write(struct.pack("<5B", *has))
            for m in e["maps"]:
                if m is not None:
                    f.write(m.astype("<i2").tobytes())
        f.write(struct.pack("<I", len(kit.morphs)))
        for mo in kit.morphs:
            w_str(f, mo["name"])
            f.write(struct.pack("<HI", mo["part"], len(mo["verts"])))
            for j, vi, pos, nrm in mo["verts"]:
                f.write(struct.pack("<HH6f", j, vi, *pos, *nrm))
        f.write(struct.pack("<I", len(kit.catalog)))
        for c in kit.catalog:
            w_str(f, c["label"])
            f.write(struct.pack("<BBBH", c["kind"], 1 if c["zoom"] else 0, c["arg"], len(c["items"])))
            for it in c["items"]:
                w_str(f, it["label"])
                f.write(struct.pack("<B", it["bodies"]))
                k = c["kind"]
                if k == 0:
                    f.write(struct.pack("<IH", it["bits"], len(it["parts"])))
                    f.write(struct.pack("<%dH" % len(it["parts"]), *it["parts"]))
                    f.write(struct.pack("<H", len(it["hat"])))
                    f.write(struct.pack("<%dH" % len(it["hat"]), *it["hat"]))
                    f.write(struct.pack("<H", len(it["hides"])))
                    for body, prim, tris in it["hides"]:
                        f.write(struct.pack("<BHI", body, prim, len(tris)))
                        f.write(struct.pack("<%dI" % len(tris), *tris))
                elif k == 1:
                    f.write(struct.pack("<h", it["tex"]))
                elif k == 2:
                    f.write(struct.pack("<h", it["morph"]))
                elif k == 3:
                    f.write(struct.pack("<BBB", *it["rgb"]))
    print("wrote %s (%.1f MB: %d textures, %d parts, %d morphs, %d categories)" % (
        path, os.path.getsize(path) / 1e6, len(kit.textures), len(kit.parts), len(kit.morphs), len(kit.catalog)))


if __name__ == "__main__":
    main()
