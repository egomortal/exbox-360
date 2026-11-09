"""Draw the dashboard's glossy Xbox 360-style button glyphs (128x128 PNGs).

Original artwork generated here, so it can ship with the source. Face-button
colours are the ones WebOS NXE measured off retail dashboard 9199
(app/src/prompts.ts). Run: python3 tools/make_shiny_glyphs.py [font.ttf]
Output: assets/shiny/*.png
"""
from pathlib import Path
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

S = 512  # drawn 4x, saved at 128
OUT = Path(__file__).resolve().parent.parent / "assets" / "shiny"
FONT = sys.argv[1] if len(sys.argv) > 1 else str(
    Path(__file__).resolve().parents[2] / "native-app" / "third_party" / "fonts" / "Inter-SemiBold.ttf")


def hexrgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vertical(size, top, bottom):
    g = Image.new("RGB", (1, size[1]))
    for y in range(size[1]):
        g.putpixel((0, y), mix(top, bottom, y / max(1, size[1] - 1)))
    return g.resize(size)


def radial(size, inner, outer, cx, cy, r):
    img = Image.new("RGB", size, outer)
    d = ImageDraw.Draw(img)
    steps = 60
    for i in range(steps, 0, -1):
        t = i / steps
        rr = r * t
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=mix(inner, outer, t))
    return img.filter(ImageFilter.GaussianBlur(S // 60))


def gloss(shape_mask, box, strength=150):
    """A soft white highlight over the top half, clipped to the shape."""
    x0, y0, x1, y1 = box
    hl = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(hl)
    w, h = x1 - x0, y1 - y0
    d.ellipse([x0 + w * 0.12, y0 + h * 0.04, x1 - w * 0.12, y0 + h * 0.58], fill=strength)
    hl = hl.filter(ImageFilter.GaussianBlur(S // 40))
    fade = vertical((S, S), (255, 255, 255), (0, 0, 0)).convert("L")
    hl = ImageChops.multiply(hl, fade)
    return ImageChops.multiply(hl, shape_mask)


def shadow(mask, offset=10, blur=14, alpha=110):
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    m = mask.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: v * alpha // 255)
    sh.paste((0, 0, 0, 255), (0, offset), m)
    return sh


def label(img, text, colour, size, cy=None, emboss=(255, 255, 255, 90)):
    font = ImageFont.truetype(FONT, size)
    d = ImageDraw.Draw(img)
    l, t, r, b = d.textbbox((0, 0), text, font=font)
    x = (S - (r - l)) / 2 - l
    y = (cy if cy is not None else S / 2) - (b - t) / 2 - t
    d.text((x, y + S // 128), text, font=font, fill=emboss)
    d.text((x, y), text, font=font, fill=colour)


def face(letter, fill, ring, ink):
    fill, ring, ink = hexrgb(fill), hexrgb(ring), hexrgb(ink)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    outer = Image.new("L", (S, S), 0)
    ImageDraw.Draw(outer).ellipse([24, 20, S - 24, S - 28], fill=255)
    img.alpha_composite(shadow(outer))
    # Silver bezel, then the coloured dome inside it.
    bezel = vertical((S, S), (236, 238, 234), (120, 126, 120))
    img.paste(bezel, (0, 0), outer)
    dome = Image.new("L", (S, S), 0)
    ImageDraw.Draw(dome).ellipse([58, 54, S - 58, S - 62], fill=255)
    img.paste(Image.new("RGB", (S, S), ring), (0, 0), dome)
    inner = Image.new("L", (S, S), 0)
    ImageDraw.Draw(inner).ellipse([70, 66, S - 70, S - 74], fill=255)
    body = radial((S, S), mix(fill, (255, 255, 255), 0.35), mix(fill, ring, 0.55), S * 0.5, S * 0.62, S * 0.42)
    img.paste(body, (0, 0), inner)
    label(img, letter, ink + (255,), 250, cy=S * 0.5)
    img.paste((255, 255, 255), (0, 0), gloss(inner, (70, 66, S - 70, S - 74), 170))
    return img


def capsule(text, top, bottom, ink, rounded_top=True, trigger=False, text_size=170):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    if trigger:
        # A trigger seen from the front: rounded top, narrowing toward the bottom.
        d.rounded_rectangle([96, 40, S - 96, S - 70], radius=120, fill=255)
        d.rectangle([96, 220, S - 96, S - 70], fill=0)
        d.polygon([(96, 220), (S - 96, 220), (S - 136, S - 70), (136, S - 70)], fill=255)
        d.rounded_rectangle([136, S - 150, S - 136, S - 70], radius=40, fill=255)
        box = (96, 40, S - 96, S - 70)
    else:
        d.rounded_rectangle([28, 128, S - 28, S - 128], radius=110 if rounded_top else 60, fill=255)
        box = (28, 128, S - 28, S - 128)
    img.alpha_composite(shadow(m))
    rim = Image.new("RGB", (S, S), mix(hexrgb(bottom), (0, 0, 0), 0.35))
    img.paste(rim, (0, 0), m)
    inner = m.filter(ImageFilter.MinFilter(17))
    img.paste(vertical((S, S), hexrgb(top), hexrgb(bottom)), (0, 0), inner)
    cy = (box[1] + box[3]) / 2 + (25 if trigger else 0)
    label(img, text, hexrgb(ink) + (255,), text_size, cy=cy, emboss=(0, 0, 0, 90))
    img.paste((255, 255, 255), (0, 0), gloss(inner, box, 120))
    return img


def small_round(top, bottom, draw_mark):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).ellipse([60, 56, S - 60, S - 64], fill=255)
    img.alpha_composite(shadow(m))
    img.paste((70, 74, 70), (0, 0), m)
    inner = m.filter(ImageFilter.MinFilter(19))
    img.paste(vertical((S, S), hexrgb(top), hexrgb(bottom)), (0, 0), inner)
    draw_mark(ImageDraw.Draw(img))
    img.paste((255, 255, 255), (0, 0), gloss(inner, (60, 56, S - 60, S - 64), 130))
    return img


def arrow(direction):
    def mark(d):
        c, h = S / 2, 82
        if direction > 0:
            d.polygon([(c - h * 0.7, c - h), (c - h * 0.7, c + h), (c + h, c)], fill=(40, 44, 40))
        else:
            d.polygon([(c + h * 0.7, c - h), (c + h * 0.7, c + h), (c - h, c)], fill=(40, 44, 40))
    return mark


def guide_orb():
    def mark(d):
        c, r, w = S / 2, 96, 34
        d.line([(c - r, c - r), (c + r, c + r)], fill=(232, 245, 220), width=w)
        d.line([(c - r, c + r), (c + r, c - r)], fill=(232, 245, 220), width=w)
    return small_round("#9be04a", "#2f7a0a", mark)


def dpad():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    a, b = 176, S - 176
    d.rounded_rectangle([a, 40, b, S - 48], radius=34, fill=255)
    d.rounded_rectangle([40, a - 4, S - 40, b - 4], radius=34, fill=255)
    img.alpha_composite(shadow(m))
    img.paste((78, 84, 78), (0, 0), m)
    inner = m.filter(ImageFilter.MinFilter(15))
    img.paste(vertical((S, S), (214, 218, 212), (128, 134, 126)), (0, 0), inner)
    dd = ImageDraw.Draw(img)
    c = S / 2 - 4
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        tx, ty, s = S / 2 + dx * 140, c + dy * 140, 28
        if dy:
            dd.polygon([(tx - s, ty - dy * s * 0.6), (tx + s, ty - dy * s * 0.6), (tx, ty + dy * s)], fill=(64, 70, 64))
        else:
            dd.polygon([(tx - dx * s * 0.6, ty - s), (tx - dx * s * 0.6, ty + s), (tx + dx * s, ty)], fill=(64, 70, 64))
    img.paste((255, 255, 255), (0, 0), gloss(inner, (40, 40, S - 40, S - 48), 110))
    return img


def stick(text):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).ellipse([34, 30, S - 34, S - 38], fill=255)
    img.alpha_composite(shadow(m))
    img.paste((52, 56, 52), (0, 0), m)
    ring = m.filter(ImageFilter.MinFilter(25))
    img.paste(vertical((S, S), (112, 118, 110), (44, 48, 44)), (0, 0), ring)
    cap = Image.new("L", (S, S), 0)
    ImageDraw.Draw(cap).ellipse([104, 100, S - 104, S - 108], fill=255)
    img.paste(radial((S, S), (150, 156, 148), (60, 64, 60), S * 0.5, S * 0.42, S * 0.3), (0, 0), cap)
    if text:
        label(img, text, (240, 244, 236, 255), 130, cy=S / 2 - 4, emboss=(0, 0, 0, 120))
    img.paste((255, 255, 255), (0, 0), gloss(ring, (34, 30, S - 34, S - 38), 90))
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    glyphs = {
        "a": face("A", "#5DBB0C", "#2A6A06", "#0F3000"),
        "b": face("B", "#D41010", "#8A0C10", "#3A0004"),
        "x": face("X", "#1468E0", "#0F428F", "#04204F"),
        "y": face("Y", "#FFE000", "#A08000", "#4A3800"),
        "lb": capsule("LB", "#e4e7e2", "#8d948a", "#2c302b"),
        "rb": capsule("RB", "#e4e7e2", "#8d948a", "#2c302b"),
        "lt": capsule("LT", "#5d635b", "#25281f", "#eef2ea", trigger=True, text_size=140),
        "rt": capsule("RT", "#5d635b", "#25281f", "#eef2ea", trigger=True, text_size=140),
        "start": small_round("#eef0ec", "#8a9088", arrow(1)),
        "back": small_round("#eef0ec", "#8a9088", arrow(-1)),
        "guide": guide_orb(),
        "dpad": dpad(),
        "stick": stick(""),
        "ls": stick("L"),
        "rs": stick("R"),
    }
    for name, img in glyphs.items():
        img.resize((128, 128), Image.LANCZOS).save(OUT / f"{name}.png", optimize=True)
        print("wrote", OUT / f"{name}.png")


if __name__ == "__main__":
    main()
