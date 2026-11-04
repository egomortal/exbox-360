#!/usr/bin/env python3
"""Make a compact front/left/back/right review from four 24-frame sweeps."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("sweeps", nargs=4, type=Path)
    args = parser.parse_args()
    labels = ["Bayonetta 1", "Bayonetta 2", "Bayonetta 3", "Young Cereza + Cheshire"]
    angles = [(0, "front"), (6, "left"), (12, "back"), (18, "right")]
    cell_w, cell_h, title_h, row_h = 230, 350, 34, 388
    sheet = Image.new("RGB", (cell_w * 4, row_h * 4), (210, 225, 202))
    draw = ImageDraw.Draw(sheet)
    for row, (label, directory) in enumerate(zip(labels, args.sweeps)):
        for column, (index, angle) in enumerate(angles):
            source = Image.open(directory / f"image-model-{index}.ppm").convert("RGB")
            sheet.paste(source.resize((cell_w, cell_h), Image.Resampling.LANCZOS),
                        (column * cell_w, row * row_h + title_h))
            title = f"{label}  |  {angle}" if column == 0 else angle
            draw.text((column * cell_w + 8, row * row_h + 9), title, fill=(22, 23, 25))
    sheet.save(args.output)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
