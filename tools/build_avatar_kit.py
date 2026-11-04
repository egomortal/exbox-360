#!/usr/bin/env python3
"""Builds Ex-Box 360's avatar kit (assets/avatar/kit.nxak) from Quaternius's
free CC0 packs, for the avatar editor on the console.

    python3 tools/build_avatar_kit.py UBC_DIR OUTFITS_DIR UAL_DIR [OUT]

UBC_DIR      "Universal Base Characters[Standard]" (unzipped)
OUTFITS_DIR  "Modular Character Outfits - Fantasy[Standard]"
UAL_DIR      "Universal Animation Library[Standard]"
OUT          default assets/avatar/kit.nxak

All three are CC0 1.0 (public domain), by Quaternius: https://quaternius.com
Free downloads: quaternius.itch.io/universal-base-characters,
/modular-character-outfits-fantasy, /universal-animation-library.

What it writes (little-endian; read by avatar_kit.hpp):
  "NXAK", u32 version, then
  joints:    u32 n; per joint: name, i16 parent
  textures:  u32 n; per texture: name, u16 w, u16 h, f32 ref[3] (average colour
             where alpha marks it tintable), u8 rgba[w*h*4]
  bodies:    u32 n; per body: name, f32 rest[J][12], f32 brow_y, f32 top_y,
             u32 clips; per clip: name, u16 frames, f32 fps, f32 pose[frames][J][12]
  parts:     u32 n; per part: name, u8 body (0, 1, 255 = either), u8 slot,
             f32 ibm[J][12], u32 prims; per primitive: i16 texture, u8 tint,
             u8 flags (1 double-sided, 2 has regions), u32 verts, then per vertex
             f32 pos[3] nrm[3] uv[2], u8 joints[4], u8 weights[4]; u32 tris,
             u32 idx[tris*3], [u8 region[tris]]
Strings are u8 length + bytes. Matrices are 3x4, row-major. Poses hold each
joint's model-space transform per frame, already retargeted onto that body.

Animations are retargeted from the library's mannequin by world-space rotation
deltas (each bone points the same way it does in the clip) keeping the body's
own bone lengths. A wave is made here from the idle clip; the library has none.
SPDX-License-Identifier: GPL-3.0-or-later (this script; the art stays CC0)
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gltf_read import Gltf, quat_mat  # noqa: E402

VERSION = 1
FPS = 30

# Slots: what a part is, as the editor sorts them.
SLOT = {"body": 0, "eyes": 1, "brows": 2, "hair": 3, "beard": 4, "top": 5, "sleeves": 6, "legs": 7,
        "feet": 8, "hat": 9, "glasses": 10, "extra": 11}
# Only hair, brows, hoods, trousers and shoes are drawn from both sides (open shells);
# everything else drops faces turned away, which halves the drawing.
# How a primitive takes the look's colours.
TINT = {"none": 0, "skin": 1, "hair": 2, "cloth": 3, "eyes": 4}
# Body regions, for clothes made from the body itself and for hiding skin
# under clothes. Kept in step with avatar_kit.hpp.
REGIONS = ["head_top", "head", "neck", "chest", "belly", "hips", "upperarm_a", "upperarm_b", "lowerarm",
           "hand", "thigh_a", "thigh_b", "calf", "foot"]
R = {n: i for i, n in enumerate(REGIONS)}

CLIPS = [("idle", "Idle_Loop"), ("talk", "Idle_Talking_Loop"), ("dance", "Dance_Loop"),
         ("interact", "Interact"), ("jump", "Jump_Start")]


def w_str(f, s):
    b = s.encode()
    f.write(struct.pack("<B", len(b)) + b)


def m34(m):
    return np.asarray(m[:3, :4], np.float32).reshape(-1)


def quat_from_mat(m):
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [s / 4, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, s / 4, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, s / 4, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def slerp(a, b, t):
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = np.dot(a, b)
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        r = a + (b - a) * t
        return r / np.linalg.norm(r)
    th = np.arccos(np.clip(d, -1, 1))
    return (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)


def rot_between(u, v):
    u, v = u / np.linalg.norm(u), v / np.linalg.norm(v)
    c = np.cross(u, v)
    d = np.dot(u, v)
    if d < -0.9999:
        axis = np.cross(u, [0, 1, 0]) if abs(u[1]) < 0.9 else np.cross(u, [1, 0, 0])
        axis /= np.linalg.norm(axis)
        return axis_angle(axis, np.pi)
    k = np.array([[0, -c[2], c[1]], [c[2], 0, -c[0]], [-c[1], c[0], 0]])
    return np.eye(3) + k + k @ k * (1 / (1 + d))


def axis_angle(axis, ang):
    axis = np.asarray(axis, float) / np.linalg.norm(axis)
    k = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
    return np.eye(3) + np.sin(ang) * k + (1 - np.cos(ang)) * k @ k


class Skeleton:
    """The 65 joints, in the base body's order."""

    def __init__(self, g):
        sk = g.j["skins"][0]
        self.nodes = sk["joints"]
        self.names = [g.nodes[j]["name"] for j in self.nodes]
        idx = {n: i for i, n in enumerate(self.nodes)}
        self.parent = [idx.get(g.parent[n], -1) for n in self.nodes]


def skin_of(g, joint_names):
    """A file's inverse bind matrices in the kit's joint order, and its own joint
    index -> kit joint index."""
    sk = g.j["skins"][0]
    ibm = g.acc(sk["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
    names = [g.nodes[j]["name"] for j in sk["joints"]]
    remap = np.array([joint_names.index(n) for n in names])
    out = np.tile(np.eye(4), (len(joint_names), 1, 1))
    for k, kit in enumerate(remap):
        out[kit] = ibm[k]
    return out, remap


def body_rest(g, sk):
    return np.array([g.world(n) for n in sk.nodes])


def retarget(ual, clip_name, body_g, sk, frames=None):
    """Model-space joint transforms of the body per frame."""
    a = next(x for x in ual.j["animations"] if x["name"] == clip_name)
    tracks = {}
    t_end = 0
    for c in a["channels"]:
        s = a["samplers"][c["sampler"]]
        times = ual.acc(s["input"])[:, 0]
        vals = ual.acc(s["output"])
        tracks[(ual.nodes[c["target"]["node"]]["name"], c["target"]["path"])] = (times, vals)
        t_end = max(t_end, times[-1])
    n = frames or max(2, int(round(t_end * FPS)) + 1)
    ual_idx = {nm: ual.node_index(nm) for nm in sk.names}
    rest_b = body_rest(body_g, sk)
    rest_u = np.array([ual.world(ual_idx[nm]) for nm in sk.names])
    pelvis = sk.names.index("pelvis")
    height_ratio = rest_b[pelvis][1, 3] / rest_u[pelvis][1, 3]

    def sample(name, path, t, default):
        tr = tracks.get((name, path))
        if tr is None:
            return np.asarray(default, float)
        times, vals = tr
        if t <= times[0]:
            return vals[0].astype(float)
        if t >= times[-1]:
            return vals[-1].astype(float)
        k = np.searchsorted(times, t) - 1
        f = (t - times[k]) / (times[k + 1] - times[k])
        if path == "rotation":
            return slerp(vals[k], vals[k + 1], f)
        return vals[k] * (1 - f) + vals[k + 1] * f

    out = []
    for i in range(n):
        t = min(t_end, i / FPS)
        # The mannequin's pose: local matrices of every node that animates.
        loc = {}
        for nm, ni in ual_idx.items():
            node = ual.nodes[ni]
            tt = sample(nm, "translation", t, node.get("translation", [0, 0, 0]))
            rr = sample(nm, "rotation", t, node.get("rotation", [0, 0, 0, 1]))
            m = np.eye(4)
            m[:3, :3] = quat_mat(rr)
            m[:3, 3] = tt
            loc[ni] = m
        world_u = np.array([ual.world(ual_idx[nm], loc) for nm in sk.names])
        world_b = np.zeros_like(rest_b)
        for j in range(len(sk.names)):
            delta = world_u[j][:3, :3] @ rest_u[j][:3, :3].T
            rot = delta @ rest_b[j][:3, :3]
            p = sk.parent[j]
            if p < 0:
                pos = rest_b[j][:3, 3]
            elif j == pelvis:
                pos = world_u[j][:3, 3] * height_ratio
                pos = np.array([pos[0], pos[1], pos[2] + (rest_b[j][2, 3] - rest_u[j][2, 3])])
            else:
                # Keep this body's bone: the rest offset from the parent, turned by the parent.
                off = np.linalg.inv(rest_b[p]) @ rest_b[j]
                pos = (world_b[p] @ np.append(off[:3, 3], 1))[:3]
            m = np.eye(4)
            m[:3, :3] = rot
            m[:3, 3] = pos
            world_b[j] = m
        out.append(world_b)
    return out


def make_wave(idle, sk):
    """The right hand raised and waving, eased in and out over the idle."""
    names = sk.names
    ua, la, hd = names.index("upperarm_r"), names.index("lowerarm_r"), names.index("hand_r")
    n = 66
    out = []
    for i in range(n):
        base = idle[i % len(idle)].copy()
        ease = min(1.0, i / 12, (n - 1 - i) / 12)
        ease = ease * ease * (3 - 2 * ease)
        phase = np.sin((i / FPS) * 2 * np.pi * 1.6)
        up_dir = np.array([-0.55, 0.78, 0.30])
        fore_dir = np.array([-0.08 + 0.32 * phase, 1.0, 0.18])
        for j, target in ((ua, up_dir), (la, fore_dir)):
            cur = base[j][:3, :3]
            child = names.index("lowerarm_r") if j == ua else hd
            bone = base[child][:3, 3] - base[j][:3, 3]
            want = rot_between(bone, target) @ cur
            q = slerp(quat_from_mat(cur), quat_from_mat(want), ease)
            new_rot = quat_mat(q)
            delta = new_rot @ cur.T
            pivot = base[j][:3, 3].copy()
            # Turn this joint and everything below it about the joint.
            for k in range(len(names)):
                kk, under = k, False
                while kk >= 0:
                    if kk == j:
                        under = True
                        break
                    kk = sk.parent[kk]
                if under:
                    base[k][:3, :3] = delta @ base[k][:3, :3]
                    base[k][:3, 3] = pivot + delta @ (base[k][:3, 3] - pivot)
        out.append(base)
    return out


def texture(path, size, mask="all"):
    im = Image.open(path).convert("RGB").resize((size, size), Image.LANCZOS)
    rgb = np.asarray(im, np.float32)
    if mask == "skin":
        # Skin takes the tone; painted underwear (grey, white, black) does not.
        mx, mn = rgb.max(2), rgb.min(2)
        sat = (mx - mn) / np.maximum(mx, 1)
        a = np.clip((sat - 0.12) / 0.10, 0, 1) * np.clip((mx - 40) / 30, 0, 1)
    elif mask == "iris":
        h, w = rgb.shape[:2]
        yy, xx = np.mgrid[0:h, 0:w]
        # The iris: the dark, saturated disc in the middle.
        lum = rgb.mean(2)
        cy, cx = np.unravel_index(np.argmin(np.where((yy - h / 2) ** 2 + (xx - w / 2) ** 2 < (h / 3) ** 2, lum, 999)), lum.shape)
        r = np.hypot(yy - cy, xx - cx)
        a = np.clip((h * 0.13 - r) / (h * 0.02), 0, 1)
    else:
        a = np.ones(rgb.shape[:2], np.float32)
    w = a[..., None]
    ref = (rgb * w).sum((0, 1)) / max(1e-6, w.sum())
    rgba = np.dstack([rgb, a * 255]).round().clip(0, 255).astype(np.uint8)
    return rgba, ref / 255.0


def vertex_region(name, p, rest, names):
    def along(a, b):
        pa, pb = rest[names.index(a)][:3, 3], rest[names.index(b)][:3, 3]
        d = pb - pa
        return float(np.dot(p - pa, d) / np.dot(d, d))

    side = name[-1] if name[-2:] in ("_l", "_r") else ""
    if name == "Head":
        return "head"
    if name == "neck_01":
        return "neck"
    if name in ("spine_03",) or name.startswith("clavicle"):
        return "chest"
    if name in ("spine_01", "spine_02"):
        return "belly"
    if name in ("pelvis", "root"):
        return "hips"
    if name.startswith("upperarm"):
        return "upperarm_a" if along("upperarm_" + side, "lowerarm_" + side) < 0.5 else "upperarm_b"
    if name.startswith("lowerarm"):
        return "lowerarm"
    if name.startswith("thigh"):
        return "thigh_a" if along("thigh_" + side, "calf_" + side) < 0.42 else "thigh_b"
    if name.startswith("calf"):
        return "calf"
    if name.startswith(("foot", "ball")):
        return "foot"
    return "hand"  # hand and fingers


def glasses_mesh(eye_l, eye_r, front_z, width, square):
    """Original frames: two rims, a bridge and arms back to the ears, skinned to Head."""
    verts, tris = [], []

    def box(a, b, t):
        # A square tube from a to b, t thick.
        a, b = np.asarray(a, float), np.asarray(b, float)
        d = b - a
        d /= np.linalg.norm(d)
        u = np.cross(d, [0, 0, 1.0]) if abs(d[2]) < 0.9 else np.cross(d, [0, 1.0, 0])
        u /= np.linalg.norm(u)
        v = np.cross(d, u)
        base = len(verts)
        for p in (a, b):
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                pos = p + (u * su + v * sv) * t / 2
                nrm = u * su + v * sv
                verts.append((pos, nrm / np.linalg.norm(nrm)))
        for k in range(4):
            k2 = (k + 1) % 4
            tris.extend([(base + k, base + k2, base + 4 + k2), (base + k, base + 4 + k2, base + 4 + k)])

    rx, ry = width * 0.5, width * (0.36 if square else 0.42)
    for c in (eye_l, eye_r):
        steps = 4 if square else 14
        pts = []
        for k in range(steps):
            ang = 2 * np.pi * (k + (0.5 if square else 0)) / steps
            if square:
                pts.append(np.array([c[0] + rx * np.sign(np.cos(ang)), c[1] + ry * np.sign(np.sin(ang)), front_z]))
            else:
                pts.append(np.array([c[0] + rx * np.cos(ang), c[1] + ry * np.sin(ang), front_z]))
        for k in range(steps):
            box(pts[k], pts[(k + 1) % steps], 0.006)
    inner = sorted([eye_l, eye_r], key=lambda e: e[0])
    box([inner[0][0] + rx, inner[0][1] + ry * 0.3, front_z], [inner[1][0] - rx, inner[1][1] + ry * 0.3, front_z], 0.005)
    for c, s in ((inner[0], -1), (inner[1], 1)):
        x = c[0] + s * rx
        box([x, c[1] + ry * 0.5, front_z], [x + s * 0.012, c[1] + ry * 0.5, front_z - 0.11], 0.005)
    P = np.array([v[0] for v in verts])
    N = np.array([v[1] for v in verts])
    return P, N, np.array(tris)


class Writer:
    def __init__(self, sk):
        self.sk = sk
        self.textures = []  # (name, rgba, ref)
        self.tex_index = {}
        self.parts = []

    def tex(self, key, path, size, mask):
        if key not in self.tex_index:
            rgba, ref = texture(path, size, mask)
            self.tex_index[key] = len(self.textures)
            self.textures.append((key, rgba, ref))
        return self.tex_index[key]

    def add_gltf_part(self, name, body, slot, path, tex_sizes, tint_for, regions=None, only_meshes=None):
        g = Gltf(path)
        ibm, remap = skin_of(g, self.sk.names)
        prims = []
        for node in g.nodes:
            if "mesh" not in node:
                continue
            mesh = g.j["meshes"][node["mesh"]]
            if only_meshes is not None and node["name"] not in only_meshes:
                continue
            for p in mesh["primitives"]:
                mat = g.j["materials"][p["material"]]
                t = mat.get("pbrMetallicRoughness", {}).get("baseColorTexture")
                tex = -1
                tint = tint_for(mat["name"])
                if t is not None:
                    uri = g.j["images"][g.j["textures"][t["index"]]["source"]]["uri"]
                    img = find_image(g.dir, uri)
                    mask = {"skin": "skin", "eyes": "iris"}.get(tint, "all")
                    tex = self.tex(uri + ":" + mask, img, tex_sizes(mat["name"]), mask)
                P = g.acc(p["attributes"]["POSITION"]).astype(np.float32)
                Nn = g.acc(p["attributes"]["NORMAL"]).astype(np.float32)
                UV = g.acc(p["attributes"]["TEXCOORD_0"]).astype(np.float32) if "TEXCOORD_0" in p["attributes"] else np.zeros((len(P), 2), np.float32)
                Jn = remap[g.acc(p["attributes"]["JOINTS_0"]).astype(int)]
                W = g.acc(p["attributes"]["WEIGHTS_0"]).astype(np.float32)
                W = W / np.maximum(W.sum(1, keepdims=True), 1e-6)
                Wb = np.round(W * 255).astype(int)
                Wb[np.arange(len(Wb)), Wb.argmax(1)] += 255 - Wb.sum(1)
                I = g.acc(p["indices"]).astype(np.uint32).reshape(-1, 3)
                reg = None
                if regions is not None:
                    reg = regions(P, Jn, W, I)
                prims.append(dict(tex=tex, tint=TINT[tint], flags=(1 if slot in (SLOT["hair"], SLOT["beard"], SLOT["brows"], SLOT["hat"], SLOT["legs"], SLOT["feet"]) else 0),
                                  P=P, N=Nn, UV=UV, J=Jn.astype(np.uint8), W=Wb.clip(0, 255).astype(np.uint8), I=I, R=reg))
        self.parts.append(dict(name=name, body=body, slot=slot, ibm=ibm, prims=prims))

    def write(self, out, bodies):
        with open(out + ".part", "wb") as f:
            f.write(b"NXAK" + struct.pack("<I", VERSION))
            f.write(struct.pack("<I", len(self.sk.names)))
            for n, p in zip(self.sk.names, self.sk.parent):
                w_str(f, n)
                f.write(struct.pack("<h", p))
            f.write(struct.pack("<I", len(self.textures)))
            for name, rgba, ref in self.textures:
                w_str(f, name)
                f.write(struct.pack("<HH3f", rgba.shape[1], rgba.shape[0], *ref))
                f.write(rgba.tobytes())
            f.write(struct.pack("<I", len(bodies)))
            for b in bodies:
                w_str(f, b["name"])
                f.write(np.concatenate([m34(m) for m in b["rest"]]).tobytes())
                f.write(struct.pack("<ff", b["brow_y"], b["top_y"]))
                f.write(struct.pack("<I", len(b["clips"])))
                for cname, frames in b["clips"]:
                    w_str(f, cname)
                    f.write(struct.pack("<Hf", len(frames), FPS))
                    f.write(np.concatenate([m34(m) for fr in frames for m in fr]).tobytes())
            f.write(struct.pack("<I", len(self.parts)))
            for p in self.parts:
                w_str(f, p["name"])
                f.write(struct.pack("<BB", p["body"], p["slot"]))
                f.write(np.concatenate([m34(m) for m in p["ibm"]]).tobytes())
                f.write(struct.pack("<I", len(p["prims"])))
                for q in p["prims"]:
                    flags = q["flags"] | (2 if q["R"] is not None else 0)
                    f.write(struct.pack("<hBBI", q["tex"], q["tint"], flags, len(q["P"])))
                    v = np.zeros(len(q["P"]), dtype=[("p", "<f4", 3), ("n", "<f4", 3), ("uv", "<f4", 2), ("j", "u1", 4), ("w", "u1", 4)])
                    v["p"], v["n"], v["uv"], v["j"], v["w"] = q["P"], q["N"], q["UV"], q["J"], q["W"]
                    f.write(v.tobytes())
                    f.write(struct.pack("<I", len(q["I"])))
                    f.write(q["I"].astype("<u4").tobytes())
                    if q["R"] is not None:
                        f.write(np.asarray(q["R"], np.uint8).tobytes())
        os.replace(out + ".part", out)


def find_image(directory, uri):
    for d in (directory, os.path.dirname(directory), os.path.join(os.path.dirname(directory), "Textures")):
        p = os.path.join(d, uri)
        if os.path.isfile(p):
            return p
    raise FileNotFoundError(uri)


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    ubc, outfits, ual_dir = sys.argv[1:4]
    out = sys.argv[4] if len(sys.argv) > 4 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "avatar", "kit.nxak")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    base = os.path.join(ubc, "Base Characters", "Godot - UE")
    hairs = os.path.join(ubc, "Hairstyles", "Rigged to Head Bone", "glTF (Godot -Unreal)")
    parts_dir = os.path.join(outfits, "Exports", "glTF (Godot-Unreal)", "Modular Parts")
    ual = Gltf(os.path.join(ual_dir, "Unreal-Godot", "UAL1_Standard.glb"))

    male = Gltf(os.path.join(base, "Superhero_Male_FullBody.gltf"))
    sk = Skeleton(male)
    wr = Writer(sk)
    bodies = []
    for bi, (bname, fname, body_mesh, brow_mesh, eye_mesh) in enumerate([
            ("male", "Superhero_Male_FullBody.gltf", "SuperHero_Male", "Eyebrows", "Eyes"),
            ("female", "Superhero_Female_FullBody.gltf", "Superhero_Female", "Eyebrows", "Eyes")]):
        path = os.path.join(base, fname)
        g = Gltf(path)
        rest = body_rest(g, sk)
        # Eyebrow height: where a beanie's rim sits.
        bnode = next(n for n in g.nodes if n.get("name") == brow_mesh)
        bp = g.acc(g.j["meshes"][bnode["mesh"]]["primitives"][0]["attributes"]["POSITION"])
        brow_y = float(bp[:, 1].max()) + 0.012
        names = sk.names

        def regions(P, Jn, W, I, rest=rest, brow_y=brow_y):
            dom = Jn[np.arange(len(Jn)), W.argmax(1)]
            vr = np.array([R["head_top"] if (names[d] == "Head" and P[k][1] > brow_y) else R[vertex_region(names[d], P[k].astype(float), rest, names)]
                           for k, d in enumerate(dom)])
            out_r = []
            for t in I:
                a, b, c = vr[t]
                out_r.append(a if a == b or a == c else (b if b == c else min(a, b, c)))
            return out_r

        size = lambda m: 1024 if "Superhero" in m else (128 if "Eye" in m else 512)
        wr.add_gltf_part(bname + "_body", bi, SLOT["body"], path, size, lambda m: "skin", regions, only_meshes=[body_mesh])
        wr.add_gltf_part(bname + "_eyes", bi, SLOT["eyes"], path, size, lambda m: "eyes", only_meshes=[eye_mesh])
        wr.add_gltf_part(bname + "_brows", bi, SLOT["brows"], path, lambda m: 256, lambda m: "hair", only_meshes=[brow_mesh])
        top_y = float(max(g.acc(p["attributes"]["POSITION"])[:, 1].max() for n in g.nodes if "mesh" in n for p in g.j["meshes"][n["mesh"]]["primitives"]))
        clips = []
        for cname, src in CLIPS:
            clips.append((cname, retarget(ual, src, g, sk)))
            print(bname, cname, len(clips[-1][1]), "frames", flush=True)
        clips.append(("wave", make_wave(clips[0][1], sk)))
        # Glasses of our own, fitted to this face.
        enode = next(n for n in g.nodes if n.get("name") == eye_mesh)
        ep = g.acc(g.j["meshes"][enode["mesh"]]["primitives"][0]["attributes"]["POSITION"])
        eye_l = ep[ep[:, 0] > 0].mean(0)
        eye_r = ep[ep[:, 0] < 0].mean(0)
        front = float(ep[:, 2].max()) + 0.022
        head_j = names.index("Head")
        for style, square in (("round", False), ("square", True)):
            P, Nn, I = glasses_mesh(eye_l, eye_r, front, 0.042 if bi == 0 else 0.039, square)
            prim = dict(tex=-1, tint=TINT["cloth"], flags=1, P=P.astype(np.float32), N=Nn.astype(np.float32),
                        UV=np.zeros((len(P), 2), np.float32), J=np.tile([head_j, 0, 0, 0], (len(P), 1)).astype(np.uint8),
                        W=np.tile([255, 0, 0, 0], (len(P), 1)).astype(np.uint8), I=I.astype(np.uint32), R=None)
            wr.parts.append(dict(name=f"{bname}_glasses_{style}", body=bi, slot=SLOT["glasses"],
                                 ibm=np.array([np.linalg.inv(m) for m in rest]), prims=[prim]))
        bodies.append(dict(name=bname, rest=rest, brow_y=brow_y, top_y=top_y, clips=clips))

    tint_hair = lambda m: "hair"
    for f in sorted(os.listdir(hairs)):
        if not f.endswith(".gltf") or f.startswith("Eyebrows"):
            continue
        stem = f[:-5]
        slot = SLOT["beard"] if "Beard" in stem else SLOT["hair"]
        wr.add_gltf_part(stem.lower(), 255, slot, os.path.join(hairs, f), lambda m: 512, tint_hair)
    outfit_slot = {"Body": "top", "Arms": "sleeves", "Legs": "legs", "Feet": "feet", "Feet_Boots": "feet",
                   "Head_Hood": "hat", "Acc_Pauldron": "extra", "Acc_Pauldrons": "extra"}
    for f in sorted(os.listdir(parts_dir)):
        if not f.endswith(".gltf"):
            continue
        stem = f[:-5]
        gender, style, piece = stem.split("_", 2)
        bi = 0 if gender == "Male" else 1
        tint = lambda m: "skin" if "Regular" in m else "cloth"
        wr.add_gltf_part(stem.lower(), bi, SLOT[outfit_slot[piece]], os.path.join(parts_dir, f),
                         lambda m: 512, tint)
    wr.write(out, bodies)
    print("wrote", out, os.path.getsize(out), "bytes;", len(wr.textures), "textures,", len(wr.parts), "parts")


if __name__ == "__main__":
    main()
