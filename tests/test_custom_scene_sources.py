"""Decode every owner package copy, and check private runtime exports against it.

Usage: python tests/test_custom_scene_sources.py PATH/TO/decrypted/resources
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT/'tools/xur5'))
from custom_scenes import decode, isolated, members, sources, walk
from compile_custom_scenes import xml, loading_loop, BUILDS
from classes5 import L as L5
from xur5 import D
from classes8 import L as L8
from xur8 import D8

ROOT = Path(sys.argv.pop(1))


class CustomSources(unittest.TestCase):
    def scene(self, build, module, name):
        return dict(members((ROOT/build/'dash.xex'/module).read_bytes()))[name]

    def test_every_copy_matches_the_recorded_decode_or_exact_blocker(self):
        baseline = json.loads((PROJECT/'private/custom-scenes/audit.json').read_text())
        expected = {(r['package'], r['member']): r for r in baseline}
        visited, copies, decoded = set(), 0, 0
        unique_decoded = set()
        for package, name, data in sources(ROOT):
            key = (package.relative_to(ROOT).as_posix(), name)
            copies += 1
            with self.subTest(package=key[0], member=name):
                row = expected[key]
                self.assertEqual(hashlib.sha256(data).hexdigest(), row['sha256'])
                if row['decoded']:
                    decoded += 1
                    unique_decoded.add(row['sha256'])
                    _, d = decode(data)
                    self.assertEqual(d.at, d.end)
                    if isinstance(d, D8) and d.counts is not None:
                        self.assertEqual(d.got, d.counts)
                else:
                    self.assertIn(name, ('GamerCardSlotScene.xur', 'FamilyCenterSlotScene.xur'))
                    self.assertIn('XuiAvatar', row['blocker'])
                    with self.assertRaises((AssertionError, KeyError, IndexError, ValueError, struct.error)) as failure:
                        decode(data)
                    self.assertEqual(str(failure.exception), row['blocker'])
                visited.add(key)
        self.assertEqual(visited, set(expected))
        self.assertEqual(copies, 1530)
        self.assertEqual(decoded, 1453)
        self.assertEqual(len(unique_decoded), 140)

    def test_v7_matches_the_same_scenes_shipped_in_v5_and_v8(self):
        beta = 'Kinect/Beta/11025 (11511.0 XDK)'
        for module, name in (('controlp', 'PanelScene.xur'), ('music', 'Equalizer.xur')):
            old, d7 = decode(self.scene(beta, module, name))
            new, d8 = decode(self.scene('Kinect/Beta/12293', module, name))
            self.assertEqual(old, new)
            self.assertEqual(d7.version, 7)
            for section in ('STRN', 'FLOT', 'VECT', 'DATA'):
                a, na = d7.parts[section]; b, nb = d8.parts[section]
                self.assertEqual(d7.d[a:a+na], d8.d[b:b+nb])
        equalizer, _ = decode(self.scene('Blades/Devkit/6683 (6534.4 XDK)', 'music', 'Equalizer.xur'))
        self.assertEqual(old, equalizer)
        # Optional v7 counts are ten u32s, unlike v8's twelve packed counts.
        _, counted = decode(self.scene(beta, 'controlp', 'LegendScene.xur'))
        self.assertEqual(counted.got, counted.counts)
        bad = bytearray(counted.d)
        struct.pack_into('>I', bad, 20, counted.counts[0]+1)
        with self.assertRaisesRegex(AssertionError, 'header count'):
            decode(bytes(bad))

    def test_v7_extra_curve_parameters_remain_unsupported(self):
        tree, d = decode(self.scene('Kinect/Beta/11025 (11511.0 XDK)', 'slots', 'GamesSlotScene.xur'))
        curves = [t['v7_curve_parameters'] for n in walk(tree)
                  for t in n.get('timelines', {}).get('tracks', []) if 'v7_curve_parameters' in t]
        self.assertTrue(curves)
        self.assertTrue(all(pair == [0., 0.] for curve in curves for pair in curve if pair is not None))
        with self.assertRaisesRegex(ValueError, 'Unsupported v7 curve parameters'):
            xml(tree, d)

    def test_all_five_skins_have_complete_data_and_visual_context(self):
        for build, path in BUILDS.items():
            with self.subTest(build=build):
                data = self.scene(path, 'dashuisk', 'skin.xur')
                tree, d = decode(data)
                self.assertEqual(d.at, d.end)
                if isinstance(d, D8):
                    self.assertEqual(d.got, d.counts)
                for identity in ('equalizer_small', 'Loading_Large'):
                    node, isolated_d, start, end = isolated(data, identity)
                    self.assertEqual(node, next(n for n in walk(tree) if n.get('props', {}).get('Id') == identity))
                    self.assertEqual(isolated_d.at, isolated_d.end)
                    self.assertLess(start, end)
                    self.assertLessEqual(end, d.end)

    def test_sound_scalar_width_prevents_false_timeline_string_indices(self):
        data = self.scene('NXE/Retail/9199', 'dashuisk', 'skin.xur')
        tree, _ = decode(data)
        sounds = [t for n in walk(tree) for t in n.get('timelines', {}).get('tracks', [])
                  if 'UndecodedSound0' in t['props']]
        self.assertTrue(sounds)
        for track in sounds:
            index = track['props'].index('UndecodedSound0')
            self.assertTrue(all(k[3][index].startswith('RAW32@') for k in track['keys']))
        import copy
        wrong = copy.deepcopy(dict(L5))
        wrong['XuiSoundXAudio'][1][0] = ('WrongString', 's')
        with self.assertRaises((AssertionError, IndexError, KeyError)):
            D(data, wrong).decode()

    def test_new_typed_properties_are_required_by_section_accounting(self):
        import copy
        data = self.scene('Metro/V2/Retail/17559', 'slots', 'CarouselSlotScene.xur')
        wrong = copy.deepcopy(L8)
        wrong['XuiGridPanel'][1][3] = ('UndecodedGrid3', 'raw')
        with self.assertRaisesRegex(AssertionError, 'unreferenced.*FLOT'):
            D8(data, wrong).decode()
        data = self.scene('Kinect/Retail/13604', 'dashuisk', 'skin.xur')
        wrong = copy.deepcopy(L8)
        wrong['XuiListItem'][-1][4] = ('UndecodedList4', 'raw')
        with self.assertRaisesRegex(AssertionError, 'unreferenced.*VECT'):
            D8(data, wrong).decode()

    def test_metro_skin_float_fields_cannot_be_consumed_as_raw_scalars(self):
        import copy
        data = self.scene('Metro/V1/Retail/15574', 'dashuisk', 'skin.xur')
        tree, _ = decode(data)
        hover = next(n for n in walk(tree) if n['class'] == 'ControlPackNuiHoverButton')
        self.assertTrue(hover['props']['UndecodedHover2'].startswith('RAW@'))
        for cls, level, bit in (('XuiControl', 1, 17), ('ControlPackNuiHoverButton', -1, 3), ('ControlPackNuiHoverButton', -1, 4)):
            with self.subTest(cls=cls, bit=bit):
                wrong = copy.deepcopy(L8)
                wrong[cls][level][bit] = ('UndecodedTest', 'raw')
                with self.assertRaisesRegex(AssertionError, 'unreferenced.*FLOT'):
                    D8(data, wrong).decode()

    def test_metro_key_kinds_remain_raw_and_unexportable(self):
        for build in ('Metro/V1/Retail/15574', 'Metro/V2/Retail/17559'):
            tree, d = decode(self.scene(build, 'dashuisk', 'skin.xur'))
            kinds = {flag & 63 for _, flag, _, _ in d.keyd}
            self.assertTrue({5, 6, 8}.issubset(kinds))
            tracks = [t for n in walk(tree) for t in n.get('timelines', {}).get('tracks', [])
                      if any(f == 136 for f, _ in t.get('flags', []))]
            self.assertTrue(tracks)
            with self.assertRaisesRegex(ValueError, 'Unsupported key flags'):
                xml(tree, d)

    def test_loading_loop_excludes_only_the_unverified_entrance(self):
        for build in (15574, 17559):
            tree, d = decode(self.scene(BUILDS[build], 'dashuisk', 'skin.xur'))
            visual = next(n for n in walk(tree) if n.get('props', {}).get('Id') == 'Loading_Large')
            with self.assertRaisesRegex(ValueError, 'Unsupported key flags'):
                xml(visual, d)
            loop = loading_loop(visual)
            xml(loop, d)
            for a, b in zip(walk(visual), walk(loop)):
                self.assertEqual(a['props'], b['props'])
                if 'timelines' not in a:
                    continue
                first = next(n[1] for n in a['timelines']['named'] if n[0] == 'Loop')
                for before, after in zip(a['timelines']['tracks'], b['timelines']['tracks']):
                    self.assertEqual(after['keys'], [k for k in before['keys'] if k[0] >= first])
            base = PROJECT/f'private/era-skins/{build}/custom-scenes'
            manifest = json.loads((base/'sources.json').read_text())
            self.assertTrue(manifest['results']['loading']['loop_only'])
            self.assertTrue((base/'loadingRing.png').is_file())

    def test_v8_legend_float_is_required_by_the_value_table(self):
        data = self.scene('Metro/V2/Retail/17559', 'controlp', 'LegendScene.xur')
        tree, _ = decode(data)
        custom = next(n for n in walk(tree) if n['class'] == 'ControlPackLegendScene')
        self.assertEqual(custom['props']['Legend0'], -2.)
        wrong = dict(L8)
        wrong['ControlPackLegendScene'] = L8['XuiScene'] + [{0: ('UndecodedLegend0', 'raw')}]
        with self.assertRaisesRegex(AssertionError, 'unreferenced.*FLOT'):
            D8(data, wrong).decode()

    def test_shader_vectors_are_typed_but_opaque_scalars_stay_opaque(self):
        tree, d = decode(self.scene('NXE/Retail/9199', 'controlp', 'AuraScene.xur'))
        perspective = next(n for n in walk(tree) if n['class'] == 'XuiPerspectiveScene')
        self.assertEqual(perspective['props']['UndecodedPerspective0'], 'RAW32@00000000')
        rings = next(n for n in walk(tree) if n.get('props', {}).get('Id') == 'Rings_Pulse')
        track = rings['timelines']['tracks'][0]
        self.assertEqual(track['props'], ['ShaderVector0', 'ShaderVector3'])
        self.assertEqual(len(track['keys'][0][3][0]), 3)
        self.assertNotIn('Scale', track['props'])
        with self.assertRaises(AssertionError):
            d.vect(d.parts['VECT'][1]//12)
        self.assertNotIn('DashScene', dict(L5))

    def test_panel_source_string_and_shader_vector_are_required(self):
        for build in ('NXE/Retail/9199', 'Metro/V2/Retail/17559'):
            data = self.scene(build, 'controlp', 'PanelScene.xur')
            tree, _ = decode(data)
            image = next(n for n in walk(tree) if n.get('props', {}).get('Id') == 'Reflection')
            self.assertEqual(image['props']['SourceElement'], 'ReflectedItems')
        wrong = dict(L8)
        level = dict(L8['XuiShader'][1])
        level[10] = ('UndecodedShader10', 'raw')
        wrong['XuiShader'] = [L8['XuiShader'][0], level]
        with self.assertRaisesRegex(AssertionError, 'unreferenced.*VECT'):
            D8(data, wrong).decode()

    def test_truncated_scenes_cannot_produce_animation_packs(self):
        for build in ('NXE/Retail/9199', 'Metro/V2/Retail/17559'):
            data = self.scene(build, 'controlp', 'LegendScene.xur')
            with self.assertRaises((AssertionError, IndexError, struct.error)):
                decode(data[:-1])

    def test_unsupported_v8_flags_are_not_silently_linearized(self):
        tree, d = decode(self.scene('Metro/V2/Retail/17559', 'controlp', 'LegendScene.xur'))
        with self.assertRaisesRegex(ValueError, 'Unsupported key flags'):
            xml(tree, d)

    def test_exported_keys_and_signed_easing_match_nxe_source(self):
        base = PROJECT/'private/era-skins/9199/custom-scenes'
        data = (base/'GamesSlotScene.nxui').read_bytes()
        self.assertEqual(data[:8], b'NXUI0002')
        nodes, animations, tracks, keys, _, _, _, _ = struct.unpack_from('<8I', data, 8)
        at = 40 + nodes*(64+32*20) + animations*28 + tracks*24
        packed = [struct.unpack_from('<fifff4fI', data, at+i*40) for i in range(keys)]
        tree, _ = decode(self.scene('NXE/Retail/9199', 'slots', 'GamesSlotScene.xur'))
        owner = tree['children'][0]
        source = next(t for t in owner['timelines']['tracks'] if t['target'] == 'imgIcon')
        for frame, mode, ease, values in source['keys']:
            signed = lambda v: v-256 if v>127 else v
            expected = (float(frame), mode, float(signed(ease[0])), float(signed(ease[1])), float(ease[2]))
            self.assertTrue(any(k[:5] == expected and all(abs(a-b)<.001 for a,b in zip(k[5:8], values[0])) for k in packed))
        self.assertTrue(any(k[2] == -100 for k in packed))

    def test_equalizer_is_a_recovered_visual_not_just_a_binding(self):
        data = self.scene('NXE/Retail/9199', 'dashuisk', 'skin.xur')
        visual, _, start, end = isolated(data, 'equalizer_small')
        self.assertLess(start, end)
        self.assertEqual(len(visual['children']), 4)
        self.assertEqual([n[0] for n in visual['timelines']['named']],
                         ['Normal', 'EndNormal', 'NormalDisable', 'EndNormalDisable'])
        for build in (6770, 9199, 13604, 15574, 17559):
            directory = PROJECT/f'private/era-skins/{build}/custom-scenes'
            manifest = json.loads((directory/'sources.json').read_text())
            for source in manifest['sources']:
                self.assertEqual(hashlib.sha256((directory/source['file']).read_bytes()).hexdigest(), source['sha256'])
            for result in manifest['results'].values():
                if 'pack' in result:
                    report = json.loads((directory/result['pack']).with_suffix('.json').read_text())
                    self.assertFalse(report['unhandled_properties'])


if __name__ == '__main__':
    unittest.main()
