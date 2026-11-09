#!/usr/bin/env python3
"""Trace a transparent concept cutout for an exact front view and alignment guide."""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="transparent source character PNG")
    parser.add_argument("texture", type=Path, help="power-of-two transparent front-view texture")
    parser.add_argument("--manifest", type=Path, help="outline samples and source bounding box")
    parser.add_argument("--outline", type=Path, help="traced silhouette as SVG")
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGBA")
    rgba = np.asarray(source)
    mask = np.uint8(rgba[..., 3] > 128)
    yy, xx = np.where(mask)
    if len(xx) < 1000:
        raise ValueError("source needs a substantial transparent character cutout")
    box = [int(xx.min()), int(yy.min()), int(xx.max() + 1), int(yy.max() + 1)]
    content_width = round((box[2] - box[0]) / (box[3] - box[1]) * 1024)
    atlas_width = 512 if content_width <= 512 else 1024
    if content_width > atlas_width:
        raise ValueError("source aspect exceeds 1024x1024 atlas")
    crop = source.crop(tuple(box)).resize((content_width, 1024), Image.Resampling.LANCZOS)
    atlas = Image.new("RGBA", (atlas_width, 1024), (0, 0, 0, 0))
    atlas.paste(crop, ((atlas_width - content_width) // 2, 0))
    atlas.save(args.texture)

    # Scanline landmarks make silhouette corrections repeatable: normalized
    # left/right bounds at fixed heights can be compared to the app render.
    scanlines = []
    for fraction in np.linspace(0.025, 0.975, 39):
        y = int(round(box[1] + fraction * (box[3] - box[1] - 1)))
        xs = np.flatnonzero(mask[y, box[0]:box[2]])
        if len(xs):
            scanlines.append({"height": round(float(fraction), 4),
                              "left": round(float(xs.min() / (box[2] - box[0])), 5),
                              "right": round(float((xs.max() + 1) / (box[2] - box[0])), 5)})
    if args.manifest:
        args.manifest.write_text(json.dumps({"source": args.source.name,
                                             "source_size": list(source.size),
                                             "source_box": box,
                                             "texture_size": [atlas_width, 1024],
                                             "texture_content_width": content_width,
                                             "scanlines": scanlines}, indent=2) + "\n")
    if args.outline:
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        paths = []
        for contour in contours:
            if cv2.contourArea(contour) < 20:
                continue
            points = cv2.approxPolyDP(contour, 1.0, True)[:, 0, :]
            paths.append("M " + " L ".join(f"{x},{y}" for x, y in points) + " Z")
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {source.width} {source.height}">\n'
               '<g fill="none" stroke="#f33" stroke-width="2">\n'
               + "\n".join(f'<path d="{d}"/>' for d in paths)
               + '\n</g>\n</svg>\n')
        args.outline.write_text(svg)
    print(f"Traced source box {box}; {len(scanlines)} scanlines; texture {args.texture}")


if __name__ == "__main__":
    main()
