#!/usr/bin/env python3
"""Pack a UV-textured image reconstruction as a rotatable NXAK character."""
import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image

from expand_bayonetta import (encode_part, encode_texture, ellipsoid, mesh_prim,
                              name_bytes, octa, pack, parse_kit, wear_item)


def make_cheshire(texture_offset, ibm, label):
    """Add a separate plush in Cereza's arms so it survives the 360-degree turn."""
    dark, light, red, gold = (63, 39, 76), (117, 77, 115), (159, 35, 56), (229, 186, 83)
    textures = [encode_texture(label + " plush " + str(i), color)
                for i, color in enumerate((dark, light, red, gold))]
    # Positions are in the importer's avatar space, in front of the torso.
    shapes = [
        ("sphere", (.17, .67, .24), (.075, .095, .06), 0),
        ("sphere", (.17, .775, .25), (.077, .070, .065), 0),
        ("octa", (.12, .84, .25), (.032, .065, .033), 0),
        ("octa", (.22, .84, .25), (.032, .065, .033), 0),
        ("sphere", (.14, .78, .304), (.017, .016, .008), 3),
        ("sphere", (.20, .78, .304), (.017, .016, .008), 3),
        ("sphere", (.17, .745, .309), (.039, .021, .01), 1),
        ("sphere", (.17, .716, .309), (.030, .016, .010), 2),
        ("sphere", (.092, .67, .24), (.025, .065, .025), 0),
        ("sphere", (.248, .67, .24), (.025, .065, .025), 0),
        ("sphere", (.137, .575, .24), (.027, .055, .027), 0),
        ("sphere", (.203, .575, .24), (.027, .055, .027), 0),
        ("octa", (.17, .69, .307), (.037, .028, .012), 2),
    ]
    prims = []
    for shape, location, size, color in shapes:
        snug_location = (location[0], location[1], location[2] - .10)
        vertices, triangles = (ellipsoid if shape == "sphere" else octa)(*snug_location, *size)
        prims.append(mesh_prim(vertices, triangles, texture_offset + color, 0))
    return textures, encode_part(label + " Cheshire plush", 1, 11, ibm, prims)


def read_obj(path):
    source_vertices, source_uv, faces = [], [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        tokens = line.split()
        if not tokens:
            continue
        if tokens[0] == "v" and len(tokens) >= 4:
            source_vertices.append(tuple(map(float, tokens[1:4])))
        elif tokens[0] == "vt" and len(tokens) >= 3:
            source_uv.append(tuple(map(float, tokens[1:3])))
        elif tokens[0] == "f" and len(tokens) >= 4:
            corners = []
            for field in tokens[1:]:
                split = field.split("/")
                vi = int(split[0]); ti = int(split[1]) if len(split) > 1 and split[1] else vi
                corners.append((vi-1 if vi > 0 else len(source_vertices)+vi,
                                ti-1 if ti > 0 else len(source_uv)+ti))
            faces.extend((corners[0], corners[i], corners[i+1]) for i in range(1, len(corners)-1))
    if not source_vertices or not source_uv or not faces:
        raise ValueError("expected a UV-textured OBJ with vertices and faces")
    dedup, vertices, uv, indices = {}, [], [], []
    for face in faces:
        triangle = []
        for key in face:
            if key not in dedup:
                dedup[key] = len(vertices)
                vertices.append(source_vertices[key[0]])
                uv.append(source_uv[key[1]])
            triangle.append(dedup[key])
        indices.append(triangle)
    return np.asarray(vertices, np.float32), np.asarray(uv, np.float32), np.asarray(indices, np.uint32)


def make_part(obj, texture_id, ibm, label, height=1.73, mirror_x=False, source_axes="legacy"): 
    source, uv, faces = read_obj(obj)
    if source_axes == "y-up":
        source = source[:, [2, 0, 1]]
    elif source_axes != "legacy":
        raise ValueError("source_axes must be legacy or y-up")
    if not np.isfinite(source).all() or not np.isfinite(uv).all():
        raise ValueError("Mesh positions and UV coordinates must be finite")
    span = float(np.ptp(source[:, 2]))
    if span <= 1e-8:
        raise ValueError("Mesh must have nonzero height on its selected up axis")
    scale = height / span
    mid_y = (source[:, 1].max()+source[:, 1].min())/2
    mid_x = (source[:, 0].max()+source[:, 0].min())/2
    vertices = np.column_stack(((source[:, 1]-mid_y)*scale,
                                (source[:, 2]-source[:, 2].min())*scale,
                                (source[:, 0]-mid_x)*scale)).astype(np.float32)
    if mirror_x:
        vertices[:, 0] *= -1
        faces = faces[:, ::-1].copy()
    # UV seams duplicate positions. Share geometric normals across those copies
    # so lighting stays smooth instead of showing every tiny texture island.
    _, welded = np.unique(np.round(vertices, 5), axis=0, return_inverse=True)
    merged_normals = np.zeros((int(welded.max()) + 1, 3), dtype=np.float32)
    for a,b,c in faces:
        n = np.cross(vertices[b]-vertices[a],vertices[c]-vertices[a])
        merged_normals[welded[a]] += n
        merged_normals[welded[b]] += n
        merged_normals[welded[c]] += n
    normals = merged_normals[welded]
    normals /= np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-10)
    vb = bytearray()
    for p,n,t in zip(vertices,normals,uv):
        vb += pack("<8f8B",*p,*n,float(t[0]),float(1.0-t[1]),0,0,0,0,255,0,0,0)
    prim = (texture_id, 0, 1, len(vertices), bytes(vb), len(faces), faces.astype("<u4").tobytes())
    return encode_part(label, 1, 11, ibm, [prim]), len(vertices), len(faces)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("kit", type=Path)
    p.add_argument("obj", type=Path)
    p.add_argument("texture", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--label", default="Bayonetta 1 (image model)")
    p.add_argument("--height", type=float, default=1.73,
                   help="avatar height in metres; use a smaller value for a child")
    p.add_argument("--mirror-x", action="store_true",
                   help="match the projection cameras to the app screen handedness")
    p.add_argument("--source-axes", choices=("legacy", "y-up"), default="legacy",
                   help="y-up accepts standard X/Y/Z OBJ coordinates without avatar body fitting")
    p.add_argument("--face-detail", type=Path,
                   help="optional 256x256 front-facing facial detail from the original concept")
    p.add_argument("--front-trace", type=Path,
                   help="optional 512x1024 or 1024x1024 traced cutout for the exact front view")
    p.add_argument("--cheshire", action="store_true",
                   help="add a separate 3D plush to the child image model")
    args = p.parse_args()
    source = args.kit.read_bytes()
    kit = parse_kit(source)
    if not .6 <= args.height <= 2.2:
        raise ValueError("height must be between 0.6 and 2.2 metres")
    if any(item["label"] == args.label for c in kit["categories"] if c[0] == "Image Model"
           for item in c[3]):
        raise ValueError(f"Image Model already contains {args.label}")
    rgba = Image.open(args.texture).convert("RGBA")
    if rgba.width & (rgba.width-1) or rgba.height & (rgba.height-1):
        raise ValueError("texture width and height must be powers of two")
    texture_data = (name_bytes(args.label+" texture")
                    + pack("<HH3f",rgba.width,rgba.height,1.0,1.0,1.0)
                    + rgba.tobytes())
    added_textures = [texture_data]
    if args.face_detail:
        face = Image.open(args.face_detail).convert("RGBA")
        if face.size != (256, 256):
            raise ValueError("face detail texture must be 256x256")
        added_textures.append(name_bytes(args.label + " face detail")
                              + pack("<HH3f", 256, 256, 1.0, 1.0, 1.0)
                              + face.tobytes())
    if args.front_trace:
        trace = Image.open(args.front_trace).convert("RGBA")
        if trace.size not in ((512, 1024), (1024, 1024)):
            raise ValueError("front trace texture must be 512x1024 or 1024x1024")
        added_textures.append(name_bytes(args.label + " front trace")
                              + pack("<HH3f", trace.width, trace.height, 1.0, 1.0, 1.0)
                              + trace.tobytes())
    part_id = len(kit["parts"])
    part, verts, faces = make_part(args.obj,len(kit["tex"]),kit["parts"][1][3],args.label,args.height, args.mirror_x, args.source_axes)
    added_parts = [part]
    item_parts = [part_id]
    if args.cheshire:
        toy_textures, toy_part = make_cheshire(len(kit["tex"]) + len(added_textures),
                                               kit["parts"][1][3], args.label)
        added_textures.extend(toy_textures)
        added_parts.append(toy_part)
        item_parts.append(part_id + 1)
    none = name_bytes("None") + pack("<BIHHH",3,0,0,0,0)
    new_item = wear_item(args.label,item_parts,[])
    category = (name_bytes("Image Model") + pack("<BBBH",0,0,5,2)
                + none + new_item)
    had_category = any(c[0] == "Image Model" for c in kit["categories"])
    if had_category:
        catalog = bytearray()
        for label, kind, raw, items in kit["categories"]:
            if label != "Image Model":
                catalog += raw
                continue
            if kind != 0:
                raise ValueError("Image Model category is not a wearable selector")
            count_at = len(name_bytes(label)) + 3
            catalog += (raw[:count_at] + pack("<H",len(items)+1)
                        + raw[count_at+2:] + new_item)
    else:
        catalog = source[kit["catalog_count_at"]+4:kit["catalog_end_at"]] + category
    result = (source[:kit["tex_count_at"]] + pack("<I",len(kit["tex"])+len(added_textures))
              + source[kit["texture_data_at"]:kit["texture_end"]] + b"".join(added_textures)
              + source[kit["texture_end"]:kit["part_count_at"]]
              + pack("<I",part_id+len(added_parts)) + source[kit["part_data_at"]:kit["part_end"]] + b"".join(added_parts)
              + source[kit["part_end"]:kit["catalog_count_at"]]
              + pack("<I",len(kit["categories"])+(0 if had_category else 1))
              + catalog + source[kit["catalog_end_at"]:])
    parsed = parse_kit(result)
    model_cats = [c for c in parsed["categories"] if c[0] == "Image Model"]
    assert len(model_cats) == 1 and model_cats[0][3][-1]["label"] == args.label
    args.output.write_bytes(result)
    print(f"Wrote {args.output}: {verts} vertices, {faces} triangles, {rgba.width}x{rgba.height} texture")


if __name__ == "__main__":
    main()
