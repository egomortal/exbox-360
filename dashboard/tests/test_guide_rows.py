"""T19: verify installed native Guide controls against the owner's XUR scenes.

Run with python -B tests/test_guide_rows.py -v. No source files are written.
T19_GUIDE_ROOT may override the read-only recovery root.
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/xur5'))
from guide_curves import convert_guide
from guide_pack import colour_factor

SOURCE = Path(os.environ.get('T19_GUIDE_ROOT',
    'private/guide-unpacked'))
BUILDS = ((9199, 'nxe'), (13604, 'kinect'), (15574, 'metro1'), (17559, 'metro'))


class Pack:
    def __init__(self, path):
        data = path.read_bytes()
        assert data[:8] == b'NXUI0002'
        counts = struct.unpack_from('<8I', data, 8)
        at = 40
        def records(count, fmt):
            nonlocal at
            size = struct.calcsize(fmt)
            out = [struct.unpack_from(fmt, data, at+i*size) for i in range(count)]
            at += count*size
            return out
        self.nodes = records(counts[0], '<12i4f'+'4fI'*32)
        self.animations = records(counts[1], '<5ifi')
        self.tracks = records(counts[2], '<6i')
        self.keys = records(counts[3], '<fifff4fI')
        self.ranges = records(counts[4], '<IfiI')
        records(counts[5], '<2f')
        records(counts[6], '<fI')
        self.strings = data[at:]
        assert len(self.strings) == counts[7]
        self.report = json.loads(path.with_suffix('.json').read_text())

    def text(self, token):
        return self.strings[token:self.strings.index(0, token)].decode('utf-8')

    def field(self, node, field):
        offset = 16+self.report['fields'].index(field)*5
        return self.nodes[node][offset:offset+5]

    def find(self, name, parent=None):
        return next(i for i, n in enumerate(self.nodes)
                    if self.text(n[4]) == name and (parent is None or n[0] == parent))


class GuideRows(unittest.TestCase):
    def test_authored_rows_geometry_visibility_and_legends(self):
        for build, era in BUILDS:
            with self.subTest(build=build):
                source = SOURCE/f'2.0.{build}.0/u'
                path = ROOT/f'private/era-skins/{era}/guide-{build}/guide-{build}.nxui'
                p = Pack(path)
                def load(rel):
                    root, _ = convert_guide(str(source/rel))
                    colour_factor(root)
                    return root
                main = load('hud/hud/GuideMain.xur')
                props = main.find('XuiScene/Properties')
                self.assertEqual([props.findtext('Legend'+b) for b in 'ABXY'],
                                 ['Select', 'Back', None, 'Xbox Dashboard' if build < 15574 else 'Xbox Home'])
                scenes = {'GamesTabSignedIn': 1 if build == 17559 else 2,
                          'HomeTabSignedIn': 2 if build == 17559 else 3,
                          'SettingsTabSignedIn': 4 if build == 17559 else 5}
                if build == 9199:
                    scenes['MarketplaceTab'] = 1
                # Preserve each signed-in subscene, including the 1px later offset.
                for scene, tab in scenes.items():
                    original = load('hud/hud/'+scene+'.xur').find('XuiScene')
                    parent = p.find('GuideTab'+str(tab))
                    child = p.find(original.findtext('Properties/Id'), parent)
                    pos = [float(x) for x in (original.findtext('Properties/Position') or '0,0,0').split(',')]
                    self.assertEqual(list(p.field(child, 'Position')[:3]), pos)
                    for button in 'ABXY':
                        self.assertIsNone(original.findtext('Properties/Legend'+button))
                    self.check_rows(p, original, child)
                for tab in main.iter('XuiScene'):
                    name = tab.findtext('Properties/Id') or ''
                    if name.startswith('Tab'):
                        self.check_rows(p, tab, p.find('Guide'+name))
                names = [p.text(n[4]) for n in p.nodes]
                self.assertFalse(any(n.startswith('row') and n[3:].isdigit() for n in names))
                self.assertEqual(p.report['missing_visuals'], [])
                self.assertEqual(p.report['pack_sha256'], hashlib.sha256(path.read_bytes()).hexdigest())
                audit = json.loads(path.with_suffix('.curves.json').read_text())
                self.assertEqual(audit['pack_sha256'], p.report['pack_sha256'])
                for entry in p.report['source']:
                    self.assertEqual(hashlib.sha256(Path(entry['xur']).read_bytes()).hexdigest(), entry['xur_sha256'])

    def check_rows(self, pack, original, parent):
        for row in original.findall('XuiNavButton'):
            name = row.findtext('Properties/Id')
            with self.subTest(row=name):
                node = pack.find(name, parent)
                self.assertEqual(pack.text(pack.field(node, 'Text')[4]), row.findtext('Properties/Text') or '')
                pos = [float(x) for x in (row.findtext('Properties/Position') or '0,0,0').split(',')]
                self.assertEqual(list(pack.field(node, 'Position')[:3]), pos)
                for prop in ('Width', 'Height'):
                    value = row.findtext('Properties/'+prop)
                    if value is not None:
                        self.assertEqual(pack.field(node, prop)[0], float(value))
                self.assertEqual(bool(pack.field(node, 'Show')[4]), row.findtext('Properties/Show') != 'false')
                self.assertEqual(pack.text(pack.field(node, 'Visual')[4]), row.findtext('Properties/Visual'))

    def test_mini_player_and_transition_tracks_unchanged(self):
        # T17 preserved bytes provide the pre-T19 animation baseline. Node
        # indices change when replacement rows disappear, so compare by path.
        for build, era in BUILDS:
            with self.subTest(build=build):
                path = ROOT/f'private/era-skins/{era}/guide-{build}/guide-{build}.nxui'
                current = Pack(path)
                old = Pack(ROOT/f'private/t19-guide-rows/before-packs/{build}/guide-{build}.nxui')
                def route(p, index):
                    parts = []
                    while index >= 0:
                        parts.append(p.text(p.nodes[index][4]))
                        index = p.nodes[index][0]
                    return tuple(reversed(parts))
                def tracks(p):
                    out = []
                    for anim, target, field, flags, first, count in p.tracks:
                        owner = p.animations[anim][0]
                        owner_path, target_path = route(p, owner), route(p, target)
                        if owner != 0 and p.text(p.nodes[owner][4]) != 'Tabscene' and 'scnMusic' not in owner_path:
                            continue
                        keys = []
                        for key in p.keys[first:first+count]:
                            # String field values are interned at different offsets.
                            keys.append(key[:-1]+(p.text(key[-1]) if field in (10, 19, 22, 27, 28) else key[-1],))
                        animation = p.animations[anim]
                        ranges = [(p.text(r[0]), r[1], r[2], p.text(r[3]))
                                  for r in p.ranges[animation[1]:animation[1]+animation[2]]]
                        out.append((owner_path, target_path, field, flags, keys, ranges))
                    return out
                self.assertEqual(tracks(current), tracks(old))
                self.assertEqual([current.text(n[4]) for i, n in enumerate(current.nodes) if 'scnMusic' in route(current, i)],
                                 [old.text(n[4]) for i, n in enumerate(old.nodes) if 'scnMusic' in route(old, i)])

    def test_all_referenced_textures_are_staged(self):
        for build, era in BUILDS:
            path = ROOT/f'private/era-skins/{era}/guide-{build}/guide-{build}.nxui'
            p = Pack(path)
            names = {p.text(p.field(i, 'Fill.TextureFileName')[4]) for i in range(len(p.nodes))}
            for _, _, field, _, first, count in p.tracks:
                if field == 22:
                    names.update(p.text(k[-1]) for k in p.keys[first:first+count])
            for name in names:
                if not name:
                    continue
                # Existing battery timeline references, not textures: the
                # runtime hides imgHeadsetBattery on PS5. T19 does not alter it.
                if name in {'Headset_Full.xur', 'Headset_Half.xur', 'Headset_OneFourth.xur'}:
                    self.assertTrue((SOURCE/f'2.0.{build}.0/u/hud/hud'/name).is_file())
                    baseline = Pack(ROOT/f'private/t19-guide-rows/before-packs/{build}/guide-{build}.nxui')
                    self.assertIn(name.encode()+b'\0', baseline.strings)
                    continue
                for prefix in ('sharedres://', 'file://', 'common://', 'xam://'):
                    if name.startswith(prefix):
                        name = name[len(prefix):]
                with self.subTest(build=build, texture=name):
                    self.assertTrue((path.parent/name).is_file())


if __name__ == '__main__':
    unittest.main()
