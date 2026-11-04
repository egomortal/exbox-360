"""Xbox 360 avatar asset decoder (models and textures).

Python port of the decoders in avatar-aura's avatar-formats crate (MIT,
https://github.com/panedivitasei/avatar-aura), which in turn ports Xenia's
xam/avatars code (BSD 3-Clause, Copyright Ben Vanik). Used only to read the
user's own console files for a private build; nothing here ships item data.

read_model(data) takes an uncompressed model payload (what avatar_unpack.py
writes) and returns a dict with batches (vertices, indices, shader params)
and textures. decode_texture(tex) returns (w, h, rgba bytes) or None.
"""
import math
import struct


class Bits:
    """Bits are consumed from the low end of each byte first."""

    def __init__(self, data):
        self.d = data
        self.o = 0
        self.n = len(data) * 8

    def read(self, k):
        if k == 0:
            return 0
        if self.o + k > self.n:
            raise ValueError("overrun at bit %d (+%d of %d)" % (self.o, k, self.n))
        start = self.o >> 3
        end = (self.o + k + 7) >> 3
        v = int.from_bytes(self.d[start:end], "little") >> (self.o & 7)
        self.o += k
        return v & ((1 << k) - 1)

    def u16(self):
        return self.read(16)

    def u32(self):
        return self.read(32)

    def i32(self):
        v = self.read(32)
        return v - (1 << 32) if v & 0x80000000 else v

    def f32(self):
        return struct.unpack("<f", struct.pack("<I", self.read(32)))[0]

    def align(self):
        self.o = (self.o + 7) & ~7

    def take(self, count):
        start = self.o >> 3
        if self.o + count * 8 > self.n:
            raise ValueError("take past end")
        self.o += count * 8
        return self.d[start:start + count]


class VectorSer:
    def __init__(self, s):
        r = s.f32()
        self.bx, self.by, self.bz = s.f32(), s.f32(), s.f32()
        self.cx, self.cy, self.cz = s.read(6), s.read(6), s.read(6)
        self.dx = r * 2.0
        self.dy = r * (2.0 / 3.0) * math.sqrt(6.0)
        self.dz = r * math.sqrt(3.0)

    def read(self, s):
        x, y, z = s.read(self.cx), s.read(self.cy), s.read(self.cz)
        oy = self.by + self.dy * y
        oz = self.bz + self.dz * z
        if y & 1:
            oz += self.dz / 3.0
        ox = self.bx + self.dx * x
        if (y & 1) != (z & 1):
            ox += 0.5 * self.dx
        return (ox, oy, oz)


class ValueSer:
    def __init__(self, s, bits):
        self.bits = bits
        self.base = s.read(bits)
        self.count = s.read(bits)
        if self.count > 64:
            raise ValueError("value serializer width %d" % self.count)

    def read(self, s):
        return (self.base + s.read(self.count)) & ((1 << self.bits) - 1)


def half(v):
    """Xenos half: no infinities, denormals flushed to zero."""
    m = v & 0x3FF
    e = (v >> 10) & 0x1F
    if e == 0:
        return 0.0
    val = (1.0 + m / 1024.0) * (2.0 ** (e - 15))
    return -val if v & 0x8000 else val


def decode_normal(p):
    def sext(v, b):
        sign = 1 << (b - 1)
        return (v ^ sign) - sign
    x = sext(p & 0x7FF, 11) / 1023.0
    y = sext((p >> 11) & 0x7FF, 11) / 1023.0
    z = sext((p >> 22) & 0x3FF, 10) / 511.0
    x, y, z = (max(-1.0, min(1.0, c)) for c in (x, y, z))
    n = math.sqrt(x * x + y * y + z * z)
    return (x / n, y / n, z / n) if n > 1e-6 else (0.0, 1.0, 0.0)


def read_texture(s):
    t = {
        "format": s.u32(), "width": s.u32(), "height": s.u32(),
        "total_data_size": s.u32(), "data_size": s.u32(), "layer_count": s.u32(),
        "is_empty": s.read(1) != 0, "is_tiled": s.read(1) != 0,
        "data_stride": s.u32(), "data_rows": s.u32(),
    }
    s.align()
    if t["width"] > 1024 or t["height"] > 1024 or not 1 <= t["layer_count"] <= 14:
        raise ValueError("bad texture header %r" % t)
    t["data"] = b"" if t["is_empty"] else bytes(
        s.take(t["data_stride"] * t["data_rows"] * t["layer_count"]))
    return t


def read_shader_param(s):
    p = {"type": s.u32(), "usage": s.u32()}
    if p["type"] == 1:
        p["index"] = s.u16()
        p["uv_layer"] = s.u16()
        p["flags"] = s.u32() & 0xFFFF
        s.read(64)
    else:
        p["values"] = [s.f32() for _ in range(4)]
    return p


def read_batch(s):
    b = {"shader_id": s.u32()}
    nparams = s.read(5)
    b["triangle_count"] = s.u32()
    nverts = s.u32()
    b["uv_count"] = s.u32()
    b["vertex_size"] = s.u32()
    s.u32()  # index_size
    s.u32()  # vertex_array_offset
    s.u32()  # index_array_offset
    s.align()
    if b["triangle_count"] > 8192 or nverts > 8192 or b["uv_count"] > 6:
        raise ValueError("batch out of range")
    b["params"] = []
    for _ in range(nparams):
        b["params"].append(read_shader_param(s))
        s.align()
    if s.u32() != nverts:
        raise ValueError("vertex count mismatch")
    vs = VectorSer(s)
    ns = ValueSer(s, 32)
    ws = ValueSer(s, 32)
    js = ValueSer(s, 32)
    cs = ValueSer(s, 32)
    verts = []
    for _ in range(nverts):
        pos = vs.read(s)
        nrm = ns.read(s)
        w = ws.read(s)
        j = js.read(s)
        c = cs.read(s)
        uvs = []
        for _ in range(b["uv_count"]):
            u = s.u16()
            v = s.u16()
            uvs.append((half(u), half(v)))
        verts.append({
            "pos": pos, "normal": decode_normal(nrm),
            "weights": [((w >> (8 * k)) & 0xFF) / 255.0 for k in range(4)],
            "joints": [(j >> (8 * k)) & 0xFF for k in range(4)],
            "color": c, "uvs": uvs,
        })
    b["vertices"] = verts
    s.align()
    nidx = s.u32()
    if nidx != b["triangle_count"] * 3:
        raise ValueError("index count mismatch")
    isr = ValueSer(s, 16)
    b["indices"] = [isr.read(s) for _ in range(nidx)]
    s.align()
    return b


def read_model(data):
    s = Bits(data)
    m = {k: s.u32() for k in ("cpu_size", "gpu_size", "texture_buffer_size",
                              "vertex_buffer_size", "index_buffer_size")}
    nb = s.u32()
    nt = s.u32()
    for _ in range(5):
        s.u32()
    if nb > 16 or nt > 18:
        raise ValueError("not a model payload")
    m["batches"] = []
    for _ in range(nb):
        m["batches"].append(read_batch(s))
        s.align()
    m["textures"] = []
    for _ in range(nt):
        s.u32()
        s.u32()
        m["textures"].append(read_texture(s))
        s.align()
    while True:
        n = s.u32()
        if n > 400:
            raise ValueError("vertex pair count")
        vp = ValueSer(s, 32)
        for _ in range(n):
            vp.read(s)
        s.align()
        if n & 1:
            break
    if s.o != s.n:
        raise ValueError("trailing model data (%d of %d bits)" % (s.o, s.n))
    return m


# --- textures: 16-bit swapped, pitched rows; DXT1/3/5 or A8R8G8B8 -------------

def _565(c):
    r = (c >> 11) & 31
    g = (c >> 5) & 63
    b = c & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def _color_block(blk, allow_alpha):
    c0, c1 = struct.unpack_from("<HH", blk, 0)
    p0, p1 = _565(c0), _565(c1)
    pal = [p0 + (255,), p1 + (255,)]
    if c0 > c1 or not allow_alpha:
        pal.append(tuple((2 * a + b + 1) // 3 for a, b in zip(p0, p1)) + (255,))
        pal.append(tuple((a + 2 * b + 1) // 3 for a, b in zip(p0, p1)) + (255,))
    else:
        pal.append(tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,))
        pal.append((0, 0, 0, 0))
    bits = struct.unpack_from("<I", blk, 4)[0]
    return [pal[(bits >> (2 * t)) & 3] for t in range(16)]


def _bc3_alpha(blk):
    a0, a1 = blk[0], blk[1]
    if a0 > a1:
        pal = [a0, a1] + [((7 - k) * a0 + k * a1 + 3) // 7 for k in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - k) * a0 + k * a1 + 2) // 5 for k in range(1, 5)] + [0, 255]
    bits = int.from_bytes(blk[2:8], "little")
    return [pal[(bits >> (3 * t)) & 7] for t in range(16)]


def decode_texture(t, layer=0):
    fmt = t["format"] & 0x3F
    w, h = t["width"], t["height"]
    if t["is_empty"] or not w or not h:
        return None
    if fmt == 18:
        bpb, dim = 8, 4
    elif fmt in (19, 20):
        bpb, dim = 16, 4
    elif fmt == 6:
        bpb, dim = 4, 1
    else:
        return None
    wb, hb = (w + dim - 1) // dim, (h + dim - 1) // dim
    row = wb * bpb
    data = t["data"]
    pitch = max(t["data_stride"], row)
    layer_bytes = t["data_rows"] * t["data_stride"]
    base = layer * layer_bytes
    if layer_bytes == 0 or base + (hb - 1) * pitch + row > len(data):
        pitch = row
        base = layer * pitch * hb
        if base + pitch * hb > len(data):
            return None
    raw = bytearray(data[base:min(base + pitch * hb, len(data))])
    raw[0::2], raw[1::2] = raw[1::2], raw[0::2]
    out = bytearray(w * h * 4)
    if fmt == 6:
        for y in range(h):
            for x in range(w):
                o = y * pitch + x * 4
                b, g, r, a = raw[o:o + 4]
                out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((r, g, b, a))
        return w, h, bytes(out)
    for by in range(hb):
        for bx in range(wb):
            o = by * pitch + bx * bpb
            blk = raw[o:o + bpb]
            if fmt == 18:
                tex = _color_block(blk, True)
            else:
                tex = _color_block(blk[8:16], False)
                if fmt == 19:
                    al = [((blk[t // 2] >> (4 * (t & 1))) & 15) * 17 for t in range(16)]
                else:
                    al = _bc3_alpha(blk)
                tex = [c[:3] + (a,) for c, a in zip(tex, al)]
            for yy in range(min(4, h - by * 4)):
                for xx in range(min(4, w - bx * 4)):
                    p = ((by * 4 + yy) * w + bx * 4 + xx) * 4
                    out[p:p + 4] = bytes(tex[yy * 4 + xx])
    return w, h, bytes(out)


# --- skeleton and animation -------------------------------------------------

import numpy as np  # noqa: E402

JOINT_NAMES = (
    "BASE BACKA LF_H RT_H SC_BASE BACKB LF_K LF_SC_H RT_K RT_SC_H SC_BACKA LF_A LF_C LF_SC_K NECK RT_A "
    "RT_C RT_SC_K SC_BACKB HEAD LF_S LF_T RT_S RT_T SC_NECK LF_E LF_SC_S LF_SC_TWIST_S RT_E RT_SC_S "
    "RT_SC_TWIST_S LF_E_TWIST LF_SC_E LF_W RT_E_TWIST RT_SC_E RT_W LF_FINGA LF_FINGB LF_FINGC LF_FINGD "
    "LF_PROP LF_SPECIAL LF_THUMB RT_FINGA RT_FINGB RT_FINGC RT_FINGD RT_PROP RT_SPECIAL RT_THUMB "
    "LF_FINGA1 LF_FINGB1 LF_FINGC1 LF_FINGD1 LF_THUMB1 RT_FINGA1 RT_FINGB1 RT_FINGC1 RT_FINGD1 "
    "RT_THUMB1 LF_FINGA2 LF_FINGB2 LF_FINGC2 LF_FINGD2 LF_THUMB2 RT_FINGA2 RT_FINGB2 RT_FINGC2 "
    "RT_FINGD2 RT_THUMB2").split()


class QuatSer:
    def __init__(self, s):
        self.v = VectorSer(s)

    def read(self, s):
        x, y, z = self.v.read(s)
        n = math.sqrt(x * x + y * y + z * z)
        if n < 1e-7:
            return (0.0, 0.0, 0.0, 1.0)
        k = math.sin(n) / n
        return (x * k, y * k, z * k, math.cos(n))


def quat_mat(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, q, s=(1.0, 1.0, 1.0)):
    m = np.eye(4)
    m[:3, :3] = quat_mat(q) * np.asarray(s)[None, :]
    m[:3, 3] = t
    return m


def read_skeleton(data):
    """Joints: parent index and bind pose (model space position, rotation)."""
    s = Bits(data)
    n = s.u32()
    if n > 72:
        raise ValueError("joint count")
    ps, rs = VectorSer(s), QuatSer(s)
    joints = []
    for _ in range(n):
        parent = s.read(8)
        joints.append({"parent": parent, "pos": ps.read(s), "rot": rs.read(s)})
    return joints


def joint_xforms(joints, scales=None):
    """bind_world, rest_world (scaled), rest local t, parent world scale, as
    avatar-bake's build_joint_xforms."""
    n = len(joints)
    scales = scales or [(1.0, 1.0, 1.0)] * n
    bind = [trs(j["pos"], j["rot"]) for j in joints]
    rest = [None] * n
    out = []
    for i, j in enumerate(joints):
        p = j["parent"]
        has_p = p != 255 and p < i
        local = np.linalg.inv(bind[p]) @ bind[i] if has_p else bind[i]
        posed = local @ np.diag(list(scales[i]) + [1.0])
        if has_p:
            rest[i] = rest[p] @ posed
            pws = np.linalg.norm(rest[p][:3, :3], axis=0)
        else:
            rest[i] = posed
            pws = np.ones(3)
        out.append({"bind": bind[i], "rest": rest[i], "local_t": local[:3, 3].copy(),
                    "scale": np.asarray(scales[i], float), "pws": pws,
                    "parent": p if has_p else -1})
    return out


def read_animation(data):
    """Pose set 0 (avatar joints, local TRS per frame), set 1 (carryable),
    motions and face texture layer indices (mouth, brow L/R, eye L/R)."""
    s = Bits(data)
    a = {"frames": s.u32(), "fps": s.f32()}
    a["pose_counts"] = [s.u32(), s.u32()]
    a["motion_count"] = s.u32()
    a["texture_count"] = s.u32()
    pose2, tex_off, mot_off = s.u32(), s.u32(), s.u32()
    size = s.u32()
    s.align()
    base = s.o
    if s.o + size * 8 != s.n:
        raise ValueError("animation size")

    def frame_set(kind, count, maxn):
        if s.u32() != a["frames"] or s.u32() != count or count > maxn:
            raise ValueError("animation frame set header")
        sers = [None] * count
        for i in range(count):
            if kind == "pose":
                ser = (VectorSer(s), QuatSer(s), VectorSer(s))
            elif kind == "motion":
                ser = (VectorSer(s), QuatSer(s))
            else:
                ser = (ValueSer(s, 32),)
            sers[count - 1 - i] = ser
        per = {"pose": 3 * 128 + 18 * 3, "motion": 2 * 128 + 18 * 2, "tex": 64}[kind]
        s.o += per * (maxn - count)
        frames = []
        for _ in range(a["frames"]):
            frames.append([tuple(x.read(s) for x in ser) for ser in sers])
        s.align()
        return frames

    a["pose"] = frame_set("pose", a["pose_counts"][0], 72)
    if s.o - base != pose2 * 8:
        raise ValueError("pose set 2 offset")
    a["carry"] = frame_set("pose", a["pose_counts"][1], 72)
    if s.o - base != mot_off * 8:
        raise ValueError("motion offset")
    a["motion"] = frame_set("motion", a["motion_count"], 3)
    if s.o - base != tex_off * 8:
        raise ValueError("texture offset")
    a["face"] = frame_set("tex", a["texture_count"], 5)
    return a


def pose_world(xf, frame):
    """Model-space joint matrices for one animation frame (pose set 0), with
    translations made absolute from the rest offsets, as avatar-bake does."""
    n = len(xf)
    world = [None] * n
    for i in range(n):
        x = xf[i]
        if i < len(frame):
            pos, rot, scl = frame[i]
            t = (x["local_t"] + np.asarray(pos)) * x["pws"]
            m = trs(t, rot, np.asarray(scl) * x["scale"])
        else:
            m = np.linalg.inv(world[x["parent"]]) @ x["rest"] if x["parent"] >= 0 else x["rest"]
        world[i] = world[x["parent"]] @ m if x["parent"] >= 0 else m
    return world


def _asset_id(s):
    a = s.u32()
    b = s.u16()
    c = s.u16()
    d = bytes(s.read(8) for _ in range(8))
    return (a, b, c, d)


def read_shape(data):
    """Blend shape: triangles to collapse (global indices over the target's
    batches) and replacement vertices (byte offsets into its vertex buffers)."""
    s = Bits(data)
    n = s.u32()
    if n > 8192:
        raise ValueError("shape index count")
    idx_total = s.u32()
    idx_target = _asset_id(s)
    ser = ValueSer(s, 32)
    tris = []
    for _ in range(n):
        v = ser.read(s)
        tris.append(v - (1 << 32) if v & 0x80000000 else v)
    s.align()
    nv = s.u32()
    if nv > 8192:
        raise ValueError("shape vertex count")
    vtx_total = s.u32()
    vtx_target = _asset_id(s)
    oi, ps = ValueSer(s, 32), VectorSer(s)
    ns, ws, js, cs = ValueSer(s, 32), ValueSer(s, 32), ValueSer(s, 32), ValueSer(s, 32)
    verts = []
    for _ in range(nv):
        off = oi.read(s)
        pos = ps.read(s)
        nrm, w, j, c = ns.read(s), ws.read(s), js.read(s), cs.read(s)
        verts.append({"offset": off, "pos": pos, "normal": decode_normal(nrm),
                      "weights": [((w >> (8 * k)) & 0xFF) / 255.0 for k in range(4)],
                      "joints": [(j >> (8 * k)) & 0xFF for k in range(4)]})
    s.align()
    if s.o != s.n:
        raise ValueError("trailing shape data")
    return {"tris": tris, "index_total": idx_total, "index_target": idx_target,
            "verts": verts, "vertex_total": vtx_total, "vertex_target": vtx_target}


def shape_fits(shape, model):
    """The patch sizes must match the target model, as Xenia checks."""
    it = sum(b["triangle_count"] * 6 for b in model["batches"])
    vt = sum(len(b["vertices"]) * b["vertex_size"] for b in model["batches"])
    return it == shape["index_total"] and ((vt + 127) & ~127) == shape["vertex_total"]


def shape_targets(shape, model):
    """(batch, triangle) pairs to hide and (batch, vertex, new vertex) moves."""
    offs = [0]
    for b in model["batches"]:
        offs.append(offs[-1] + b["triangle_count"])
    hide = []
    for t in shape["tris"]:
        for j in range(len(model["batches"])):
            if offs[j] <= t < offs[j + 1]:
                hide.append((j, t - offs[j]))
                break
    voffs = [0]
    for b in model["batches"]:
        voffs.append(voffs[-1] + len(b["vertices"]) * b["vertex_size"])
    moves = []
    for v in shape["verts"]:
        for j in range(len(model["batches"])):
            if voffs[j] <= v["offset"] < voffs[j + 1]:
                moves.append((j, (v["offset"] - voffs[j]) // model["batches"][j]["vertex_size"], v))
                break
    return hide, moves
