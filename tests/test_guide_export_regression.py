"""T17: rebuild all 12 owner packs, with retained private output and no skips.

python -B tests/test_guide_export_regression.py
Override sources with T17_GUIDE_ROOT (guide-unpacked) and T17_1888_SOURCE.
Output is retained under private/t17-export-regression; no owner writes/deletes.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/xur5'))
from guide_curves import LEGACY_FLAGS, POLICY, prepare_pending, write_xml
from guide_pack import C, element
from compile_xui_scene import Compiler
from toxml import convert
from compile_custom_scenes import xml as strict_xml
from xur8 import decode

BUILDS = ((1888, 'blades'), (6770, 'blades'), (9199, 'nxe'),
          (13604, 'kinect'), (15574, 'metro1'), (17559, 'metro'))
DOWNLOADS = ROOT.parents[2]
GUIDE = Path(os.environ.get('T17_GUIDE_ROOT', DOWNLOADS/'Xbox 360 Guide Recovery/guide-unpacked'))
LAUNCH = Path(os.environ.get('T17_1888_SOURCE', DOWNLOADS/'playstation personal app/1888.FS-decrypted/unpacked-corrected'))
PRIVATE = ROOT/'private/t17-export-regression'
PRIVATE.mkdir(parents=True, exist_ok=True)
OUTPUT = Path(tempfile.mkdtemp(prefix='test-', dir=PRIVATE))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class OwnerExports(unittest.TestCase):
    def test_all_twelve_exports_match_installed_packs_and_keep_source_hashes(self):
        results = []
        for build, era in BUILDS:
            source = LAUNCH if build == 1888 else GUIDE/f'2.0.{build}.0/u'
            for kind in ('guide', 'notify'):
                with self.subTest(build=build, kind=kind):
                    self.assertTrue(source.is_dir(), 'Required owner source missing: '+str(source))
                    old = ROOT/f'private/era-skins/{era}/{kind}-{build}/{kind}-{build}.nxui'
                    # Every XUR used by the installed pack must be the same source.
                    baseline = json.loads(old.with_suffix('.json').read_text())
                    hashes = {}
                    for entry in baseline['source']:
                        name = Path(entry['xur']).name
                        candidates = [p for p in source.rglob(name) if digest(p) == entry['xur_sha256']]
                        self.assertTrue(candidates, 'Original source hash not found: '+entry['xur'])
                        hashes.update((p, digest(p)) for p in candidates)
                    out = OUTPUT/str(build)/kind/f'{kind}-{build}.nxui'
                    out.parent.mkdir(parents=True, exist_ok=True)
                    script = 'compile_notify.py' if kind == 'notify' else 'compile_guide.py' if build < 9199 else 'compile_tabbed.py'
                    env = dict(os.environ, GUIDE_SOURCE=str(source), GUIDE_1888_SOURCE=str(source))
                    run = subprocess.run([sys.executable, '-B', str(ROOT/'tools/xur5'/script), str(out)],
                                         cwd=ROOT, env=env, capture_output=True, text=True)
                    (out.parent/'build.log').write_text(run.stdout+run.stderr, encoding='utf-8')
                    self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
                    self.assertEqual(out.read_bytes(), old.read_bytes(), 'Pack changed: '+str(out))
                    report = json.loads(out.with_suffix('.json').read_text())
                    self.assertEqual(report['pack_sha256'], digest(out))
                    self.assertEqual(report['missing_visuals'], baseline['missing_visuals'])
                    self.assertEqual(report['unhandled_properties'], baseline['unhandled_properties'])
                    for entry in report['source']:
                        self.assertEqual(digest(Path(entry['xur'])), entry['xur_sha256'])
                    for path, before in hashes.items():
                        self.assertEqual(digest(path), before, 'Owner source modified: '+str(path))
                    curves = out.with_suffix('.curves.json')
                    count, flags = 0, []
                    if curves.is_file():
                        audit = json.loads(curves.read_text())
                        self.assertEqual(audit['policy'], POLICY)
                        self.assertEqual(audit['pack_sha256'], digest(out))
                        self.assertEqual(audit['source'], report['source'])
                        rows = audit['approximations']
                        count = len(rows)
                        flags = sorted({r['original_flag'] for r in rows})
                        self.assertEqual(audit['exact_curves'], not rows)
                        for row in rows:
                            self.assertIn(row['original_flag'], {0, 1, 2} | LEGACY_FLAGS.keys())
                            self.assertEqual(row['ease'][2], 50)
                            if row['original_flag'] in (10, 74, 138):
                                self.assertEqual(len(row['original_curve']), 3)
                        self.assertEqual(bool(rows), kind == 'guide' and build >= 13604)
                    if kind == 'notify':
                        assets = json.loads(out.with_suffix('.assets.json').read_text())
                        for entry in assets['images']:
                            staged = out.parent/entry['file']
                            self.assertEqual(staged.read_bytes(), Path(entry['source']).read_bytes())
                            self.assertEqual(staged.read_bytes(), (old.parent/entry['file']).read_bytes())
                    results.append(dict(build=build, kind=kind, installed=str(old), generated=str(out),
                                        sha256=digest(out), bytes=out.stat().st_size,
                                        approximate_keys=count, flags=flags, comparison='byte-identical'))
        (OUTPUT/'results.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
        self.assertEqual(len(results), 12)
        print('\nT17 retained build/comparison results: '+str(OUTPUT/'results.json'))

    def test_real_later_sources_remain_rejected_by_both_strict_exporters(self):
        for build in (13604, 15574, 17559):
            for rel in ('huduiskin/skin/skin.xur', 'xam/xam/hudbkgnd.xur'):
                with self.subTest(build=build, source=rel):
                    path = GUIDE/f'2.0.{build}.0/u'/rel
                    with self.assertRaisesRegex(ValueError, 'Unsupported key flags'):
                        convert(path)
                    tree, decoder = decode(str(path))
                    with self.assertRaisesRegex(ValueError, 'Unsupported key flags'):
                        strict_xml(tree, decoder)


def timeline(parent, target, flag, prop='Opacity'):
    tl = ET.SubElement(parent, 'Timelines')
    ET.SubElement(tl, 'NamedFrames')
    track = ET.SubElement(tl, 'Timeline', GuideSource='synthetic.xur', GuideOwner='fixture')
    ET.SubElement(track, 'Id').text = target
    ET.SubElement(track, 'TimelineProp').text = prop
    key = ET.SubElement(track, 'KeyFrame')
    for name, value in (('Time', '0'), ('Interpolation', str(LEGACY_FLAGS.get(flag, 0))),
                        ('EaseIn', '0'), ('EaseOut', '0'), ('EaseScale', '50'),
                        ('Prop', '1'), ('OriginalFlag', str(flag))):
        ET.SubElement(key, name).text = value
    if flag in (10, 74, 138):
        ET.SubElement(key, 'OriginalCurveParameters').text = '[0.2, 0.3, 0.4]'
    return tl, key


class CurvePolicy(unittest.TestCase):
    def compiler(self, flag, prop='Opacity', target='child'):
        root = ET.Element('XuiScene')
        tl, key = timeline(root, target, flag, prop)
        c = C([])
        c.nodes = [dict(id='owner')]
        c.pending = [(0, tl)]
        return c, tl, key

    def test_every_unlisted_byte_flag_fails_even_on_ignored_properties_or_targets(self):
        for flag in set(range(256)) - {0, 1, 2} - LEGACY_FLAGS.keys():
            with self.subTest(flag=flag):
                c, _, _ = self.compiler(flag, 'UnknownProperty', 'AbsentTarget')
                with self.assertRaisesRegex(ValueError, 'Unsupported Guide key flag %d.*synthetic.xur' % flag):
                    prepare_pending(c)

    def test_known_fallbacks_are_audited_without_mutating_source_metadata(self):
        for flag, mode in LEGACY_FLAGS.items():
            with self.subTest(flag=flag):
                c, tl, key = self.compiler(flag)
                before = ET.tostring(tl)
                prepare_pending(c)
                self.assertEqual(ET.tostring(tl), before)
                row, = c.curve_approximations
                self.assertEqual(row['original_flag'], flag)
                self.assertEqual(row['interpolation'], mode)
                self.assertEqual(row['original_curve'], [0.2, 0.3, 0.4] if flag in (10, 74, 138) else None)
                self.assertIsNone(c.pending[0][1].find('.//OriginalFlag'))
                self.assertIsNotNone(key.find('OriginalFlag'))

    def test_nondefault_ease_is_explicit_and_unverified_coefficients_fail(self):
        c, _, key = self.compiler(66)
        key.find('EaseScale').text = '80'
        prepare_pending(c)
        self.assertEqual(c.curve_approximations[0]['original_ease'], [0., 0., 80.])
        self.assertEqual(c.curve_approximations[0]['ease'], [0., 0., 50.])
        c, _, key = self.compiler(66)
        ET.SubElement(key, 'OriginalEaseCoefficients').text = '0,0'
        with self.assertRaisesRegex(ValueError, 'Unsupported Guide original ease'):
            prepare_pending(c)

    def test_nested_and_animated_visual_dependencies_are_validated(self):
        root = ET.Element('XuiRoot')
        scene = element('XuiScene', root, Id='scene')
        element('XuiControl', scene, Id='control', Visual='base')
        base = element('XuiVisual', Id='base')
        nested = element('XuiVisual', Id='nested')
        element('XuiElement', nested, Id='child')
        timeline(nested, 'child', 136)
        main = OUTPUT/'dependency-fixture.xui'
        for route in ('unused', 'nested', 'animated'):
            with self.subTest(route=route):
                sc, visual = copy.deepcopy(scene), copy.deepcopy(base)
                if route == 'nested':
                    element('XuiControl', visual, Id='inner', Visual='nested')
                if route == 'animated':
                    _, key = timeline(sc, 'control', 0, 'Visual')
                    key.find('Prop').text = 'nested'
                doc = ET.Element('XuiRoot'); doc.append(sc)
                write_xml(doc, main)
                c = C([]); c.visuals = {'base': visual, 'nested': nested}
                if route == 'unused':
                    c.compile(main)
                    self.assertEqual(c.curve_approximations, [])
                else:
                    with self.assertRaisesRegex(ValueError, 'Unsupported Guide key flag 136'):
                        c.compile(main)
        # The common compiler must still reject preserved metadata itself.
        root = ET.Element('XuiRoot'); scene = element('XuiTabScene', root, Id='strict')
        element('XuiElement', scene, Id='child'); timeline(scene, 'child', 74)
        write_xml(root, main)
        with self.assertRaisesRegex(ValueError, 'Unverified original keyframe curve'):
            Compiler([]).compile(main)

    def test_xml_write_does_not_double_windows_source_line_breaks(self):
        root = element('XuiLabel', Text='one\r\ntwo\r\n')
        path = OUTPUT/'newline-fixture.xui'
        write_xml(root, path)
        self.assertIn(b'one\r\ntwo\r\n', path.read_bytes())
        self.assertEqual(ET.parse(path).getroot().findtext('Properties/Text'), 'one\ntwo\n')


if __name__ == '__main__':
    unittest.main()
