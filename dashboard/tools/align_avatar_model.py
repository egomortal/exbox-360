#!/usr/bin/env python3
"""Align an OBJ to Ex-Box landmarks and make it a selectable skinned NXAK part.

The OBJ only needs vertex positions and polygon faces. Supply three or more
matching source landmarks in a JSON file. The kit's feminine body supplies
skin weights to every imported vertex via nearest-surface lookup.
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np

from expand_bayonetta import encode_part, encode_texture, name_bytes, pack, parse_kit, wear_item


TARGET = {
    "head_top": [0.0, 1.83, 0.0],
    "left_shoulder": [-0.225, 1.32, 0.0],
    "right_shoulder": [0.225, 1.32, 0.0],
    "hips": [0.0, 0.91, 0.0],
    "left_foot": [-0.095, 0.015, 0.0],
    "right_foot": [0.095, 0.015, 0.0],
}


def read_obj(path):
    vertices, triangles = [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v" and len(fields) >= 4:
            vertices.append([float(x) for x in fields[1:4]])
        elif fields[0] == "f" and len(fields) >= 4:
            face = []
            for token in fields[1:]:
                raw = int(token.split("/")[0])
                face.append(raw - 1 if raw > 0 else len(vertices) + raw)
            triangles.extend((face[0], face[i], face[i+1]) for i in range(1, len(face)-1))
    if not vertices or not triangles:
        raise ValueError("OBJ needs vertices and faces")
    if any(min(t) < 0 or max(t) >= len(vertices) for t in triangles):
        raise ValueError("OBJ face index is out of range")
    return np.asarray(vertices, np.float64), np.asarray(triangles, np.uint32)


def align(vertices, markers):
    names = [name for name in TARGET if name in markers]
    if len(names) < 3:
        raise ValueError("provide at least 3 named landmarks from avatar_fit_guide.json")
    source = np.asarray([markers[name] for name in names], np.float64)
    target = np.asarray([TARGET[name] for name in names], np.float64)
    if source.shape != target.shape:
        raise ValueError("landmarks must be xyz triples")
    center_a, center_b = source.mean(axis=0), target.mean(axis=0)
    a, b = source - center_a, target - center_b
    u, singular, vt = np.linalg.svd(a.T @ b)
    correction = np.eye(3)
    correction[-1, -1] = np.linalg.det(u @ vt)
    rotation = u @ correction @ vt
    scale = singular.sum() / np.sum(a * a)
    aligned = (vertices - center_a) @ rotation * scale + center_b
    error = float(np.sqrt(np.mean(np.sum(((source-center_a) @ rotation*scale+center_b-target)**2, axis=1))))
    return aligned, error


def make_part(vertices, triangles, body_part, texture_id, label):
    _, _, _, ibm, prims = body_part
    _, _, _, count, source_vb, _, _ = prims[0]
    source = np.frombuffer(source_vb, dtype=np.uint8).reshape(count, 40)
    positions = np.frombuffer(source_vb, dtype="<f4").reshape(count, 10)[:, :3].astype(np.float64)
    normals = np.zeros_like(vertices)
    for a, b, c in triangles:
        normal = np.cross(vertices[b] - vertices[a], vertices[c] - vertices[a])
        normals[a] += normal; normals[b] += normal; normals[c] += normal
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    payload = bytearray()
    for start in range(0, len(vertices), 256):
        batch = vertices[start:start+256]
        distance = np.sum((batch[:, None, :] - positions[None, :, :])**2, axis=2)
        nearest = np.argmin(distance, axis=1)
        for i, ref in enumerate(nearest):
            index = start + i
            payload += pack("<8f", *vertices[index], *normals[index], 0.5, 0.5)
            payload += source[ref, 32:40].tobytes()
    prim = (texture_id, 0, 1, len(vertices), bytes(payload), len(triangles), triangles.astype("<u4").tobytes())
    return encode_part(label, 1, 11, ibm, [prim])


def insert_part(source, part, label, color, replace_body):
    kit = parse_kit(source)
    texture = encode_texture(label + " material", color)
    part_id = len(kit["parts"])
    hide = []
    if replace_body:
        hide = [(1, 0, list(range(kit["parts"][1][4][0][5])))]
    none = name_bytes("None") + pack("<BIHHH", 3, 0, 0, 0, 0)
    category = (name_bytes("Aligned Model") + pack("<BBBH", 0, 0, 5, 2)
                + none + wear_item(label, [part_id], hide))
    return (source[:kit["tex_count_at"]] + pack("<I", len(kit["tex"])+1)
            + source[kit["texture_data_at"]:kit["texture_end"]] + texture
            + source[kit["texture_end"]:kit["part_count_at"]]
            + pack("<I", part_id+1) + source[kit["part_data_at"]:kit["part_end"]] + part
            + source[kit["part_end"]:kit["catalog_count_at"]]
            + pack("<I", len(kit["categories"])+1)
            + source[kit["catalog_count_at"]+4:kit["catalog_end_at"]]
            + category + source[kit["catalog_end_at"]:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kit", type=Path, help="existing NXAK kit")
    parser.add_argument("obj", type=Path, help="source OBJ mesh")
    parser.add_argument("landmarks", type=Path, help="JSON with source mesh landmark xyz positions")
    parser.add_argument("output", type=Path, help="new NXAK kit")
    parser.add_argument("--label", default="Imported fitted model")
    parser.add_argument("--color", nargs=3, type=int, default=(40, 35, 55), metavar=("R", "G", "B"))
    parser.add_argument("--replace-body", action="store_true", help="hide the kit's original body mesh")
    args = parser.parse_args()
    source = args.kit.read_bytes()
    kit = parse_kit(source)
    vertices, triangles = read_obj(args.obj)
    aligned, error = align(vertices, json.loads(args.landmarks.read_text(encoding="utf-8")))
    part = make_part(aligned, triangles, kit["parts"][1], len(kit["tex"]), args.label)
    output = insert_part(source, part, args.label, args.color, args.replace_body)
    parsed = parse_kit(output)
    assert parsed["categories"][-1][0] == "Aligned Model"
    args.output.write_bytes(output)
    print(f"Aligned {len(vertices)} vertices; landmark RMS error {error:.4f}; wrote {args.output}")


if __name__ == "__main__":
    main()
