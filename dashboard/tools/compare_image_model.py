#!/usr/bin/env python3
"""Align a source cutout to an app render for a same-scale visual comparison."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="transparent original cutout")
    parser.add_argument("render", type=Path, help="actual app renderer PPM")
    parser.add_argument("output", type=Path)
    parser.add_argument("--original-picture", type=Path, help="uncut original image")
    parser.add_argument("--previous", type=Path, help="optional earlier app render")
    args = parser.parse_args()
    source = Image.open(args.source).convert("RGBA")
    render = Image.open(args.render).convert("RGB")
    bg = (210, 225, 202)
    a = np.asarray(source)[..., 3]
    sy, sx = np.where(a > 128)
    arr = np.asarray(render).astype(np.int16)
    mask = np.max(np.abs(arr - np.array(bg)), axis=2) > 28
    my, mx = np.where(mask)
    sb = (sx.min(), sy.min(), sx.max()+1, sy.max()+1)
    mb = (mx.min(), my.min(), mx.max()+1, my.max()+1)
    height = mb[3] - mb[1]
    width = round((sb[2]-sb[0])/(sb[3]-sb[1])*height)
    figure = source.crop(sb).resize((width, height), Image.Resampling.LANCZOS)
    aligned = Image.new("RGB", render.size, bg)
    aligned.paste(figure, (round((mb[0]+mb[2]-width)/2), mb[1]), figure)
    panels = []
    if args.original_picture:
        original = Image.open(args.original_picture).convert("RGB")
        original.thumbnail(render.size, Image.Resampling.LANCZOS)
        frame = Image.new("RGB", render.size, bg)
        frame.paste(original, ((render.width-original.width)//2,
                               (render.height-original.height)//2))
        panels.append(("Original picture", frame))
    panels.append(("Original aligned to app camera", aligned))
    if args.previous:
        previous = Image.open(args.previous).convert("RGB")
        if previous.size != render.size:
            raise ValueError("previous render must match current render size")
        panels.append(("Previous app render", previous))
    panels.append(("Current app render", render))
    out = Image.new("RGB", (render.width*len(panels), render.height+36), bg)
    draw = ImageDraw.Draw(out)
    for i, (title, panel) in enumerate(panels):
        out.paste(panel, (i*render.width, 36))
        draw.text((i*render.width+10, 10), title, fill="black")
    out.save(args.output)
    print(f"Compared {len(panels)} panels at {render.width}x{render.height}")


if __name__ == "__main__":
    main()
