"""Minimal glTF 2.0 reader (.gltf + .bin, or .glb) with numpy accessors, for build_avatar_kit.py.
SPDX-License-Identifier: GPL-3.0-or-later"""
import json, struct, os
import numpy as np

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


class Gltf:
    def __init__(self, path):
        self.dir = os.path.dirname(path)
        data = open(path, "rb").read()
        if data[:4] == b"glTF":
            n = struct.unpack("<I", data[12:16])[0]
            self.j = json.loads(data[20:20 + n])
            o = 20 + n
            bn = struct.unpack("<I", data[o:o + 4])[0]
            self.bins = [data[o + 8:o + 8 + bn]]
        else:
            self.j = json.loads(data)
            self.bins = [open(os.path.join(self.dir, b["uri"]), "rb").read() for b in self.j["buffers"]]
        self.nodes = self.j["nodes"]
        self.parent = [-1] * len(self.nodes)
        for i, n in enumerate(self.nodes):
            for c in n.get("children", []):
                self.parent[c] = i

    def acc(self, i):
        a = self.j["accessors"][i]
        bv = self.j["bufferViews"][a["bufferView"]]
        dt = np.dtype(CT[a["componentType"]])
        nc = NC[a["type"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", 0) or dt.itemsize * nc
        buf = self.bins[bv["buffer"]]
        cnt = a["count"]
        if stride == dt.itemsize * nc:
            arr = np.frombuffer(buf, dt, cnt * nc, off).reshape(cnt, nc)
        else:
            raw = np.frombuffer(buf, np.uint8, stride * (cnt - 1) + dt.itemsize * nc, off)
            arr = np.lib.stride_tricks.as_strided(raw, (cnt, dt.itemsize * nc), (stride, 1)).copy().view(dt).reshape(cnt, nc)
        if a.get("normalized"):
            arr = arr.astype(np.float32) / np.iinfo(dt).max
        return arr.copy()

    def local(self, i):
        n = self.nodes[i]
        if "matrix" in n:
            return np.array(n["matrix"], np.float64).reshape(4, 4).T
        return trs(n.get("translation", [0, 0, 0]), n.get("rotation", [0, 0, 0, 1]), n.get("scale", [1, 1, 1]))

    def world(self, i, locals_=None):
        m = np.eye(4)
        while i >= 0:
            l = locals_[i] if locals_ is not None and i in locals_ else self.local(i)
            m = l @ m
            i = self.parent[i]
        return m

    def node_index(self, name):
        for i, n in enumerate(self.nodes):
            if n.get("name") == name:
                return i
        return -1


def quat_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, r, s):
    m = np.eye(4)
    m[:3, :3] = quat_mat(r) @ np.diag(s)
    m[:3, 3] = t
    return m
