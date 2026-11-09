#!/usr/bin/env python3
"""Create a contact sheet and geometry coverage report from a 360-degree sweep."""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path, help="directory of image-model-N.ppm frames")
    parser.add_argument("output", type=Path, help="PNG contact sheet")
    parser.add_argument("--samples", type=int, default=24)
    parser.add_argument("--columns", type=int, default=6)
    parser.add_argument("--metrics", type=Path)
    args = parser.parse_args()
    images = [Image.open(args.frames / f"image-model-{n}.ppm").convert("RGB")
              for n in range(args.samples)]
    width, height = images[0].size
    if any(im.size != (width, height) for im in images):
        raise ValueError("all sweep frames must have the same dimensions")
    thumb_w, thumb_h = width // 2, height // 2
    rows = (args.samples + args.columns - 1) // args.columns
    sheet = Image.new("RGB", (thumb_w * args.columns, (thumb_h + 32) * rows),
                      (210, 225, 202))
    draw = ImageDraw.Draw(sheet)
    metrics = []
    bg = np.array([210, 225, 202], dtype=np.int16)
    for n, im in enumerate(images):
        x = (n % args.columns) * thumb_w
        y = (n // args.columns) * (thumb_h + 32)
        draw.text((x + 8, y + 7), f"{n * 360 / args.samples:g}°", fill="black")
        sheet.paste(im.resize((thumb_w, thumb_h), Image.Resampling.LANCZOS),
                    (x, y + 32))
        mask = np.uint8(np.max(np.abs(np.asarray(im).astype(np.int16) - bg), axis=2) > 28)
        ys, xs = np.where(mask)
        count, _, stats, _ = cv2.connectedComponentsWithStats(mask, 8)
        components = sorted((int(stats[i, cv2.CC_STAT_AREA]) for i in range(1, count)
                             if stats[i, cv2.CC_STAT_AREA] > 8), reverse=True)
        metrics.append({"angle_degrees": n * 360 / args.samples,
                        "visible_pixels": int(mask.sum()),
                        "bbox": [int(xs.min()), int(ys.min()),
                                 int(xs.max()+1), int(ys.max()+1)],
                        "components_over_8_pixels": len(components),
                        "largest_components": components[:5]})
    sheet.save(args.output)
    if args.metrics:
        args.metrics.write_text(json.dumps({"frame_size": [width, height],
                                           "samples": metrics}, indent=2) + "\n")
    print(f"Wrote {args.samples} views to {args.output}")


if __name__ == "__main__":
    main()
