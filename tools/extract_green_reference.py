#!/usr/bin/env python3
"""Make a transparent reference from a green-background character concept."""
import argparse
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--preview", type=Path, help="optional extraction on the app preview color")
    parser.add_argument("--green-cutoff", type=float, default=11.0,
                        help="minimum green channel separation treated as background")
    parser.add_argument("--transition", type=float, default=8.0,
                        help="color distance across the antialiased edge")
    args = parser.parse_args()
    source = np.asarray(Image.open(args.source).convert("RGB"))
    r, g, b = (source[..., i].astype(np.float32) for i in range(3))
    separation = np.minimum(g - r, g - b)
    alpha = np.clip((args.green_cutoff - separation) / args.transition, 0, 1)
    core = np.uint8(alpha > .5)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(core, 8)
    keep = np.zeros_like(core)
    height, width = core.shape
    for i in range(1, count):
        if (stats[i, cv2.CC_STAT_AREA] > 100 and
                stats[i, cv2.CC_STAT_LEFT] > width * .04 and
                stats[i, cv2.CC_STAT_TOP] < height * .965):
            keep[labels == i] = 1
    keep = cv2.dilate(keep, np.ones((5, 5), np.uint8))
    alpha *= keep
    rgba = np.concatenate((source, np.uint8(np.round(alpha * 255))[..., None]), axis=2)
    Image.fromarray(rgba).save(args.output)
    if args.preview:
        bg = np.array([210, 225, 202], dtype=np.float32)
        composite = (source.astype(np.float32) * alpha[..., None] +
                     bg * (1 - alpha[..., None])).astype(np.uint8)
        Image.fromarray(composite).save(args.preview)
    yy, xx = np.where(alpha > .5)
    print(f"Extracted box {(int(xx.min()), int(yy.min()), int(xx.max()+1), int(yy.max()+1))}")


if __name__ == "__main__":
    main()
