"""AV1 conversion tests: python -B tests/test_avatar360.py."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/avatar360'))
from portable import identity, inverse, mul, trs, xforms, pose_world, png_bytes, read_png, sample, material, bake
from formats import quantize
from audit import inspect_kit
from run import checked_output, apply_shape
from avatar360.convert import FLAGGED


class MathTests(unittest.TestCase):
    def test_inverse_scaled_affine(self):
        m = trs([2, -3, 7], [.2, -.3, .1, .9], [.9, 1.4, 1.1])
        for a, b in zip(mul(m, inverse(m)), identity()): self.assertAlmostEqual(a, b)

    def test_hierarchy_and_animation_offsets(self):
        js = [dict(parent=255, pos=[0, 1, 0], rot=[0, 0, 0, 1]),
              dict(parent=0, pos=[0, 3, 0], rot=[0, 0, 0, 1])]
        xf = xforms(js)
        pose = pose_world(xf, [([1, 0, 0], [0, 0, 0, 1], [1, 1, 1]), ([0, 1, 0], [0, 0, 0, 1], [1, 1, 1])])
        self.assertEqual(pose[1][3], 1)
        self.assertEqual(pose[1][7], 4)

    def test_quantized_weights_sum_and_zero(self):
        for w in ([1, 0, 0, 0], [.1, .2, .3, .4], [.001, .333, .333, .333]):
            q = quantize(w); self.assertEqual(sum(q), 255); self.assertGreaterEqual(min(q), 0)
        with self.assertRaises(ValueError): quantize([0, 0, 0, 0])


class MaterialTests(unittest.TestCase):
    def test_png_roundtrip_and_crc_rejection(self):
        px = bytes(range(24)); png = png_bytes(3, 2, px)
        self.assertEqual(read_png(png), (3, 2, px))
        bad = bytearray(png); bad[-5] ^= 1
        with self.assertRaises(ValueError): read_png(bad)

    def test_bilinear_wrap_clamp_and_transparent_border(self):
        im = (2, 1, bytes([255, 0, 0, 255, 0, 0, 255, 255]))
        self.assertEqual(sample(im, [-1, .5], 0), (1, 0, 0, 1))
        self.assertEqual(sample(im, [.5, .5], 0), (.5, 0, .5, 1))
        self.assertEqual(sample(im, [1.25, .5], 1), (1, 0, 0, 1))
        self.assertEqual(sample(im, [-.1, .5], 0, True), (0, 0, 0, 0))

    def test_recolor_and_decal_order(self):
        batch = dict(shader_id=0, params=[dict(type=1, usage=i+1, index=i, uv_layer=0, flags=0) for i in range(3)])
        images = [(1, 1, bytes(p)) for p in ([0, 0, 255, 255], [255, 0, 0, 255], [0, 255, 0, 128])]
        _, _, evaluate = material(batch, images, {22: [1, 0, 0, 1]})
        col, weight = evaluate([[.5, .5]])
        self.assertAlmostEqual(col[0], 127/255); self.assertAlmostEqual(col[1], 128/255)
        self.assertEqual(col[2], 0); self.assertAlmostEqual(weight, 127/255)

    def test_head_feature_tint(self):
        b = dict(shader_id=4, params=[dict(type=1, usage=5, index=0, uv_layer=0, flags=0),
                                    dict(type=1, usage=12, index=1, uv_layer=0, flags=0)])
        _, _, evaluate = material(b, [None, (1, 1, bytes([255, 0, 0, 255]))], {15: [.6, .2, .3, 1]})
        col, _ = evaluate([[.5, .5]])
        for a, b in zip(col, [.6, .2, .3, 1.]): self.assertAlmostEqual(a, b)

    def test_missing_uv_channels_repeat_first(self):
        b = dict(shader_id=0, uv_count=1, params=[dict(type=1, usage=1, index=0, uv_layer=5, flags=0)])
        layer, _, evaluate = material(b, [(1, 1, bytes([30, 60, 90, 255]))])
        self.assertEqual(layer, 0)
        col, _ = evaluate([[.5, .5]])
        self.assertAlmostEqual(col[0], 30/255)

    def test_stock_wristwear_wide_uvs_are_not_dropped(self):
        vertices = [dict(uvs=[uv, [.5, .5]]) for uv in ([0, 0], [11, 0], [0, 1])]
        batch = dict(shader_id=0, uv_count=2, vertices=vertices, indices=[0, 1, 2],
                     params=[dict(type=1, usage=1, index=0, uv_layer=0, flags=3),
                             dict(type=1, usage=3, index=1, uv_layer=1, flags=0)])
        _, (_, _, data) = bake(batch, [(1, 1, bytes([255, 0, 0, 255])), (1, 1, bytes([0, 0, 0, 0]))])
        self.assertGreater(sum(data[0::4]), 0)

    def test_png_sub_filter(self):
        # Independently construct PNG rows using the Sub filter.
        px = bytes([10, 20, 30, 40, 50, 60, 70, 80])
        raw = b'\1'+px[:4]+bytes((px[i]-px[i-4]) % 256 for i in range(4, 8))
        def chunk(k, v): return struct.pack('>I', len(v))+k+v+struct.pack('>I', zlib.crc32(k+v))
        png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 1, 8, 6, 0, 0, 0))+chunk(b'IDAT', zlib.compress(raw))+chunk(b'IEND', b'')
        self.assertEqual(read_png(png), (2, 1, px))


class SafetyAndIntegration(unittest.TestCase):
    def test_output_boundary(self):
        src = ROOT/'private/example-readonly'
        self.assertEqual(checked_output(src, ROOT/'private/avatar360/test')[1], (ROOT/'private/avatar360/test').resolve())
        for out in (ROOT/'private/avatar', ROOT/'tools', ROOT/'private/avatar360/../../../outside'):
            with self.assertRaises(ValueError): checked_output(src, out)
        with self.assertRaises(ValueError): checked_output(ROOT/'private/avatar360', ROOT/'private/avatar360/inside-source')

    def test_shape_refuses_wrong_target(self):
        class Decoder:
            @staticmethod
            def shape_fits(shape, model): return False
        with self.assertRaises(ValueError): apply_shape(Decoder(), {}, {})

    def test_template_nxak_structural_and_truncation(self):
        path = ROOT/'private/avatar360/av1/review/Avatar6404.nxak'
        if not path.exists(): self.skipTest('Run the template conversion first')
        data = path.read_bytes(); info = inspect_kit(data)
        self.assertEqual(info['joints'], 71); self.assertEqual(info['parts'], 6)
        self.assertEqual(len(info['clips']), 14); self.assertIn('idle', info['clips']); self.assertIn('wave', info['clips'])
        self.assertGreater(info['triangles'], 4000)
        for bad in (data[:7], data[:-1], data+b'junk'):
            with self.assertRaises(ValueError): inspect_kit(bad)

    def test_glb_structure(self):
        path = ROOT/'private/avatar360/av1/review/Avatar6404.glb'
        if not path.exists(): self.skipTest('Run the template conversion first')
        b = path.read_bytes(); magic, version, size = struct.unpack_from('<III', b)
        self.assertEqual((magic, version, size), (0x46546c67, 2, len(b)))
        size, kind = struct.unpack_from('<I4s', b, 12); self.assertEqual(kind, b'JSON')
        d = json.loads(b[20:20+size]); self.assertEqual(len(d['meshes']), 6)
        for v in d['bufferViews']: self.assertLessEqual(v['byteOffset']+v['byteLength'], d['buffers'][0]['byteLength'])
        self.assertEqual(len(d['images']), 7)

    def test_exclusions_and_hair_dependencies(self):
        root = ROOT/'private/avatar360'
        if not (root/'manifest.json').exists(): self.skipTest('No owner staging')
        m = json.loads((root/'manifest.json').read_text(encoding='utf-8')); rs = {r['index']: r for r in m['items']}
        self.assertFalse(FLAGGED & {r.get('guid') for r in rs.values()})
        added = [r for r in rs.values() if r['group'] == 'hair']; self.assertEqual(len(added), 184)
        for r in added:
            partner = rs[r['companion']]; self.assertEqual(partner['category'], 'hair')
            self.assertIn(r['companion'], r['dependencies']); self.assertTrue(partner['partner_only'])
        exclusions = json.loads((root/'exclusions.json').read_text(encoding='utf-8'))
        self.assertEqual({r['guid'] for r in exclusions['dresses']}, FLAGGED)
        self.assertTrue(exclusions['held_packages'])

    def test_full_conversion_accounting(self):
        path = ROOT/'private/avatar360/av1/wardrobe.json'
        if not path.exists(): self.skipTest('Full conversion not available')
        d = json.loads(path.read_text(encoding='utf-8'))
        self.assertTrue(d['complete']); self.assertFalse(d['failures'])
        self.assertEqual(len(d['items']), 2951)
        self.assertEqual(len({x['index'] for x in d['items']}), 2951)
        self.assertEqual(d['groups']['closet'], 1068)

    def test_marketplace_offsets(self):
        root = ROOT/'private/avatar360/av1'
        if not (root/'marketplace.offsets').exists(): self.skipTest('Disk index not available')
        index = (root/'marketplace.offsets').read_bytes(); rows = (root/'marketplace.jsonl').read_bytes()
        self.assertEqual(index[:4], b'AVIX'); version, count = struct.unpack_from('<II', index, 4)
        self.assertEqual((version, count), (1, 20221)); self.assertEqual(len(index), 12+count*12)
        previous = 0
        for i in range(count):
            offset, length = struct.unpack_from('<QI', index, 12+i*12)
            self.assertEqual(offset, previous); previous = offset+length
            self.assertEqual(json.loads(rows[offset:previous])['status'], 'staged')
        self.assertEqual(previous, len(rows))


if __name__ == '__main__': unittest.main(verbosity=2)
