"""Bakes a 360 avatar model batch's material into one texture in its output UV
set, the way avatar-aura's avatar-bake does (MIT; a port of Xenia's avatar
export). Private pipeline: reads the user's own console files only.

Body shaders (0-3): colour (usage 1) * custom recolour (usage 2 intensity:
c22*i.r + c23*i.g + c24*i.b mixed by i.a) + decal (usage 3).
Head shader (4): skin base (usage 5) + eye shadow (11), mouth (12), eyes (9/10),
facial hair (6), brows (7/8) tinted by colours 13..21:
  0 skin, 1 hair, 2 mouth, 3 iris, 4 eyebrow, 5 eye shadow, 6 facial hair,
  7/8 skin features.
"""
import numpy as np

import xavatar

WHITE = (1.0, 1.0, 1.0, 1.0)


def image(tex, layer=0):
    r = xavatar.decode_texture(tex, layer)
    if r is None:
        return None
    w, h, px = r
    return np.frombuffer(px, np.uint8).reshape(h, w, 4).astype(np.float32) / 255.0


class Sampler:
    def __init__(self, img, uv_layer, flags, transparent_border=False):
        self.img, self.l, self.flags, self.tb = img, uv_layer, flags, transparent_border

    def sample(self, uv):
        """uv: (..., 6, 2) all layers; returns (..., 4) bilinear."""
        img = self.img
        u = uv[..., self.l, 0].copy()
        v = uv[..., self.l, 1].copy()
        out_of = (u < 0) | (u > 1) | (v < 0) | (v > 1)
        if not self.flags & 1:
            u = np.clip(u, 0, 1)
        if not self.flags & 2:
            v = np.clip(v, 0, 1)
        h, w = img.shape[:2]
        fx = u * w - 0.5
        fy = v * h - 0.5
        x0 = np.floor(fx).astype(np.int64)
        y0 = np.floor(fy).astype(np.int64)
        tx = (fx - x0)[..., None]
        ty = (fy - y0)[..., None]

        def ix(x, n, wrap):
            return np.mod(x, n) if wrap else np.clip(x, 0, n - 1)
        xa, xb = ix(x0, w, self.flags & 1), ix(x0 + 1, w, self.flags & 1)
        ya, yb = ix(y0, h, self.flags & 2), ix(y0 + 1, h, self.flags & 2)
        c = (img[ya, xa] * (1 - tx) + img[ya, xb] * tx) * (1 - ty) + (img[yb, xa] * (1 - tx) + img[yb, xb] * tx) * ty
        if self.tb:
            c[out_of] = 0
        return c


def recipe(batch, images, overrides=None, replacements=None, is_head=False):
    """overrides: usage -> rgba; replacements: usage -> image (face features)."""
    tex = {}
    const = {}
    for p in batch["params"]:
        if p["type"] == 1:
            tex[p["usage"]] = p
        elif p["type"] in (2, 3):
            const[p["usage"]] = tuple(p["values"])
    const.update(overrides or {})
    head = (batch["shader_id"] == 4 or is_head) and 5 in tex

    def samp(usage):
        p = tex.get(usage)
        if p is None:
            return None
        img = (replacements or {}).get(usage)
        if img is None and p["index"] < len(images):
            img = images[p["index"]]
        if img is None:
            return None
        flags = p["flags"]
        return Sampler(img, min(p["uv_layer"], 5), flags, 6 <= usage <= 12 and flags == 0)

    r = {"head": head, "const": const}
    if not head:
        r["color"], r["intensity"], r["decal"] = samp(1), samp(2), samp(3)
        r["custom"] = [np.array(const.get(22 + k, WHITE)[:3], np.float32) for k in range(3)]
        r["out"] = r["color"].l if r["color"] else 0
    else:
        r["base"], r["shadow"], r["mouth"] = samp(5), samp(11), samp(12)
        r["eye"] = samp(9 if 9 in tex else 10)
        r["fhair"] = samp(6)
        r["brow"] = samp(7 if 7 in tex else 8)
        r["tint"] = [np.array(const.get(13 + k, WHITE)[:3], np.float32) for k in range(9)]
        r["out"] = r["base"].l if r["base"] else 0
    return r


def evaluate(r, uv):
    """uv (..., 6, 2) -> rgba (..., 4); for body shaders alpha is the colour's alpha
    and 'skin' gives how much of the texel is the custom recolour (for tinting)."""
    shape = uv.shape[:-2]
    if not r["head"]:
        c = r["color"].sample(uv) if r["color"] else np.ones(shape + (4,), np.float32)
        rgb = c[..., :3]
        recol_w = np.zeros(shape, np.float32)
        if r["intensity"]:
            i = r["intensity"].sample(uv)
            cu = r["custom"]
            rec = cu[0] * i[..., 0:1] + cu[1] * i[..., 1:2] + cu[2] * i[..., 2:3]
            rgb = rgb + (rec - rgb) * i[..., 3:4]
            recol_w = i[..., 3] * i[..., 0]
        if r["decal"]:
            d = r["decal"].sample(uv)
            rgb = rgb + (d[..., :3] - rgb) * d[..., 3:4]
            recol_w = recol_w * (1 - d[..., 3])
        return np.concatenate([np.clip(rgb, 0, 1), c[..., 3:4]], -1), recol_w
    t = r["tint"]
    skin = t[0]
    rgb = np.broadcast_to(skin, shape + (3,)).copy()
    skin_w = np.ones(shape, np.float32)
    if r["base"]:
        b = r["base"].sample(uv)
        mixed = t[7] * b[..., 0:1] + t[8] * b[..., 1:2] + skin * b[..., 2:3]
        rgb = rgb + (mixed - rgb) * b[..., 3:4]
    if r["shadow"]:
        s = r["shadow"].sample(uv)
        rgb = rgb + (t[5] * s[..., 0:1] - rgb) * s[..., 3:4]
        skin_w = skin_w * (1 - s[..., 3])

    def feature(rgb, smp, tint, skin_w):
        m = smp.sample(uv)
        f = tint * m[..., 0:1] + m[..., 1:2] + skin * m[..., 2:3]
        return rgb + (f - rgb) * m[..., 3:4], skin_w * (1 - m[..., 3])
    for key, k in (("mouth", 2), ("eye", 3), ("fhair", 6), ("brow", 4)):
        if r[key]:
            rgb, skin_w = feature(rgb, r[key], t[k], skin_w)
    return np.concatenate([np.clip(rgb, 0, 1), np.ones(shape + (1,), np.float32)], -1), skin_w


def bake(batch, r, size):
    """Rasterises the batch in its output UV set (wrapped into [0,1)) and
    evaluates the material per texel. Returns (rgba, weight, covered)."""
    W, H = size
    UV = np.array([[uv for uv in v["uvs"]] + [v["uvs"][0]] * (6 - len(v["uvs"]))
                   for v in batch["vertices"]], np.float32) if batch["uv_count"] else None
    I = np.array(batch["indices"]).reshape(-1, 3)
    out = np.zeros((H, W, 4), np.float32)
    wgt = np.zeros((H, W), np.float32)
    cov = np.zeros((H, W), bool)
    lo = r["out"]
    for tri in I:
        t = UV[tri]  # 3,6,2
        p = t[:, lo, :] * [W, H]
        x0, x1 = int(np.floor(p[:, 0].min())), int(np.ceil(p[:, 0].max()))
        y0, y1 = int(np.floor(p[:, 1].min())), int(np.ceil(p[:, 1].max()))
        if x1 - x0 > 4 * W or y1 - y0 > 4 * H:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (p[1, 0] - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (p[1, 1] - p[0, 1])
        if abs(d) < 1e-12:
            continue
        w1 = ((gx - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (gy - p[0, 1])) / d
        w2 = ((p[1, 0] - p[0, 0]) * (gy - p[0, 1]) - (gx - p[0, 0]) * (p[1, 1] - p[0, 1])) / d
        w0 = 1 - w1 - w2
        e = -0.6 / max(abs(d) ** 0.5, 1)  # include edge texels
        m = (w0 >= e) & (w1 >= e) & (w2 >= e)
        if not m.any():
            continue
        bw = np.stack([w0[m], w1[m], w2[m]], -1)
        uv = np.einsum("nk,kld->nld", bw, t)
        col, sk = evaluate(r, uv)
        xs = np.mod(gx[m].astype(np.int64), W)
        ys = np.mod(gy[m].astype(np.int64), H)
        out[ys, xs] = col
        wgt[ys, xs] = sk
        cov[ys, xs] = True
    # Grow into uncovered texels so filtering never reaches the background.
    for _ in range(4):
        if cov.all():
            break
        acc = np.zeros_like(out)
        accw = np.zeros_like(wgt)
        n = np.zeros((H, W), np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            c = np.roll(cov, (dy, dx), (0, 1))
            acc += np.roll(out, (dy, dx), (0, 1)) * c[..., None]
            accw += np.roll(wgt, (dy, dx), (0, 1)) * c
            n += c
        grow = (~cov) & (n > 0)
        out[grow] = acc[grow] / n[grow][:, None]
        wgt[grow] = accw[grow] / n[grow]
        cov |= grow
    return out, wgt, cov


def uv_map(batch, out_layer, size):
    """Every UV set at each texel of the output set (wrapped into [0,1)):
    (H, W, 6, 2) and a coverage mask. Uncovered texels are grown from their
    neighbours a few times so filtering at seams stays on the surface."""
    W, H = size
    UV = np.array([[uv for uv in v["uvs"]] + [v["uvs"][0]] * (6 - len(v["uvs"]))
                   for v in batch["vertices"]], np.float32)
    I = np.array(batch["indices"]).reshape(-1, 3)
    out = np.zeros((H, W, 6, 2), np.float32)
    cov = np.zeros((H, W), bool)
    for tri in I:
        t = UV[tri]
        p = t[:, out_layer, :] * [W, H]
        x0, x1 = int(np.floor(p[:, 0].min())), int(np.ceil(p[:, 0].max()))
        y0, y1 = int(np.floor(p[:, 1].min())), int(np.ceil(p[:, 1].max()))
        if x1 - x0 > 4 * W or y1 - y0 > 4 * H:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (p[1, 0] - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (p[1, 1] - p[0, 1])
        if abs(d) < 1e-12:
            continue
        w1 = ((gx - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (gy - p[0, 1])) / d
        w2 = ((p[1, 0] - p[0, 0]) * (gy - p[0, 1]) - (gx - p[0, 0]) * (p[1, 1] - p[0, 1])) / d
        w0 = 1 - w1 - w2
        e = -0.6 / max(abs(d) ** 0.5, 1)
        m = (w0 >= e) & (w1 >= e) & (w2 >= e)
        if not m.any():
            continue
        bw = np.stack([w0[m], w1[m], w2[m]], -1)
        uv = np.einsum("nk,kld->nld", bw, t)
        xs = np.mod(gx[m].astype(np.int64), W)
        ys = np.mod(gy[m].astype(np.int64), H)
        out[ys, xs] = uv
        cov[ys, xs] = True
    for _ in range(4):
        if cov.all():
            break
        acc = np.zeros_like(out)
        n = np.zeros((H, W), np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            c = np.roll(cov, (dy, dx), (0, 1))
            acc += np.roll(out, (dy, dx), (0, 1)) * c[..., None, None]
            n += c
        grow = (~cov) & (n > 0)
        out[grow] = acc[grow] / n[grow][:, None, None]
        cov |= grow
    return out, cov
