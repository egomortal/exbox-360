"""Validate the six private notification packs: python -B tests/test_notify_packs.py."""
import hashlib
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILDS = ((1888, 'blades'), (6770, 'blades'), (9199, 'nxe'),
          (13604, 'kinect'), (15574, 'metro1'), (17559, 'metro'))


class Pack:
    def __init__(self, path):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:8] != b'NXUI0002':
            raise ValueError('Not NXUI0002')
        counts = struct.unpack_from('<8I', self.data, 8)
        at = 40
        def records(count, fmt):
            nonlocal at
            size = struct.calcsize(fmt)
            result = [struct.unpack_from(fmt, self.data, at+i*size) for i in range(count)]
            at += count*size
            return result
        self.nodes = records(counts[0], '<12i4f'+'4fI'*32)
        self.animations = records(counts[1], '<5ifi')
        self.tracks = records(counts[2], '<6i')
        self.keys = records(counts[3], '<fifff4fI')
        self.ranges = records(counts[4], '<IfiI')
        records(counts[5], '<2f')
        records(counts[6], '<fI')
        self.strings = self.data[at:]
        if len(self.strings) != counts[7]:
            raise ValueError('Pack length mismatch')
        self.report = json.loads(path.with_suffix('.json').read_text())
        self.assets = json.loads(path.with_suffix('.assets.json').read_text())

    def text(self, token):
        return self.strings[token:self.strings.index(0, token)].decode('utf-8')


class Notifications(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.packs = [(b, Pack(ROOT/f'private/era-skins/{era}/notify-{b}/notify-{b}.nxui'))
                     for b, era in BUILDS]

    def test_complete_hierarchy_and_slots(self):
        for build, p in self.packs:
            with self.subTest(build=build):
                self.assertEqual(p.report['missing_visuals'], [])
                self.assertEqual(p.report['unhandled_properties'], [])
                self.assertEqual(p.nodes[0][16+4*5], 460)
                self.assertEqual(p.nodes[0][16+5*5], 87)
                for i, n in enumerate(p.nodes):
                    self.assertTrue(-1 <= n[0] < i)
                slots = p.assets['slots']
                for name, index in slots.items():
                    self.assertEqual(p.text(p.nodes[index][4]), name)
                self.assertEqual(p.nodes[slots['Image']][1], 4)
                self.assertEqual(p.nodes[slots['TextPresenter']][1], 5)
                self.assertEqual(p.nodes[slots['TextPresenter']][0], slots['@visual:scr_Notification'])

    def test_authored_transition_and_loop_frames(self):
        for build, p in self.packs:
            with self.subTest(build=build):
                visual = p.assets['slots']['@visual:scr_Notification']
                anim = next(a for a in p.animations if a[0] == visual)
                frames = {p.text(r[0]): r for r in p.ranges[anim[1]:anim[1]+anim[2]]}
                self.assertEqual(frames['TransTo'][1], 0)
                self.assertEqual(frames['loop'][1], 65)
                self.assertEqual(frames['EndTransTo'][1:3], (185, 3))
                self.assertEqual(p.text(frames['EndTransTo'][3]), 'loop')
                self.assertEqual(frames['TransFrom'][1], 186)
                self.assertEqual(frames['EndTransFrom'][1:3], (240, 1))
                for animation, target, field, _, first, count in p.tracks:
                    self.assertLess(animation, len(p.animations))
                    self.assertLess(target, len(p.nodes))
                    self.assertNotEqual(field, 27)  # No duplicate audio events.
                    keys = p.keys[first:first+count]
                    self.assertEqual([k[0] for k in keys], sorted(k[0] for k in keys))

    def test_every_texture_and_achievement_icon_is_staged(self):
        for build, p in self.packs:
            with self.subTest(build=build):
                names = {entry['file'] for entry in p.assets['images']}
                for n in p.nodes:
                    token = n[16+22*5+4]
                    if token:
                        self.assertIn(p.text(token).split('://')[-1], names)
                required = {'xenonLogo.png', 'secretAchievement.png', 'unearnedAchievement.png'}
                if build != 1888:
                    required.add('Achievement.png')
                self.assertTrue(required <= names)
                for entry in p.assets['images']:
                    data = (p.path.parent/entry['file']).read_bytes()
                    self.assertTrue(data.startswith(b'\x89PNG\r\n\x1a\n'))
                    self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])
                    # On the source PC, also prove copies match the owner's originals.
                    source = Path(entry['source'])
                    if source.is_file():
                        self.assertEqual(data, source.read_bytes())

    def test_pack_hashes_match_manifests(self):
        for build, p in self.packs:
            with self.subTest(build=build):
                self.assertEqual(hashlib.sha256(p.data).hexdigest(), p.report['pack_sha256'])
                for source in p.report['source']:
                    path = Path(source['xur'])
                    if path.is_file():
                        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), source['xur_sha256'])


if __name__ == '__main__':
    unittest.main()
