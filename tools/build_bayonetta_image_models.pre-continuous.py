#!/usr/bin/env python3
"""Rebuild the four-character image-model expansion from checked-in inputs."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--legacy", action="store_true",
                        help="rebuild the prior single-image 3D meshes")
    args = parser.parse_args()
    root = args.root.resolve()
    tools = root / "tools"
    avatar = root / "private" / "avatar"
    model_assets = avatar if args.legacy else avatar / "multiview"
    output = args.output or avatar / "kit360.nxak"
    variants = [
        ("Bayonetta 1 (image model)",
         "bayonetta1-image-model.obj" if args.legacy else "b1-mv-profile-import.obj",
         "bayonetta1-image-model-texture.png" if args.legacy else "b1-mv-profile-import.png",
         "bayonetta1-front-trace.png",
         ["--face-detail", str(avatar / "bayonetta1-face-detail.png")]),
        ("Bayonetta 2 (image model)",
         "bayonetta2-image-model-source.obj" if args.legacy else "b2-mv-import.obj",
         "bayonetta2-image-model-texture.png" if args.legacy else "b2-mv-import.png",
         "bayonetta2-front-trace.png", []),
        ("Bayonetta 3 (image model)",
         "bayonetta3-image-model-source.obj" if args.legacy else "b3-mv-import.obj",
         "bayonetta3-image-model-texture.png" if args.legacy else "b3-mv-import.png",
         "bayonetta3-front-trace.png", []),
        ("Young Cereza + Cheshire (image model)",
         "young-cereza-cheshire-image-model-source.obj" if args.legacy else "cereza-mv-import.obj",
         "young-cereza-cheshire-image-model-texture.png" if args.legacy else "cereza-mv-import.png",
         "young-cereza-cheshire-front-trace.png",
         ["--height", "1.15", *(["--cheshire"] if args.legacy else [])]),
    ]
    with tempfile.TemporaryDirectory(prefix="bayo-kit-") as staging:
        previous = Path(staging) / "expanded.nxak"
        subprocess.run([sys.executable, str(tools / "expand_bayonetta.py"),
                        str(avatar / "kit360.base.nxak"), str(previous)], check=True)
        for index, (label, obj, texture, trace, extra) in enumerate(variants):
            next_kit = output if index == len(variants) - 1 else Path(staging) / f"kit-{index}.nxak"
            command = [sys.executable, str(tools / "import_image_model.py"),
                       str(previous), str(model_assets / obj), str(model_assets / texture),
                       str(next_kit), "--label", label,
                       "--front-trace", str(avatar / trace), *extra]
            subprocess.run(command, check=True)
            previous = next_kit
    print(f"Four image models ready: {output}")


if __name__ == "__main__":
    main()
