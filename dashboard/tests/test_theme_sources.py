"""Exercise the isolated theme reader against the owner's read-only packages."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools/xur5'))
from theme_states import decode, members

ROOT = pathlib.Path(sys.argv.pop(1))


class ThemeSources(unittest.TestCase):
    def scene(self, relative):
        package = (ROOT / relative / 'dash.xex/controlp').read_bytes()
        return next(data for name, data in members(package) if name == 'AuraScene.xur')

    def test_floor_tracks_are_typed_not_guessed_solids(self):
        for source in ('Kinect/Retail/13604', 'Metro/V1/Retail/15574'):
            with self.subTest(source=source):
                result = decode(self.scene(source))
                self.assertTrue(result['verified'])
                group = result['group']
                self.assertEqual(group['props']['Id'], 'Color')
                self.assertEqual(len(group['timelines']['named']), 17)
                floor = next(t for t in group['timelines']['tracks'] if t['target'] == 'floor')
                self.assertEqual(floor['props'], ['Element25'])
                self.assertEqual([k[0] for k in floor['keys']], list(range(17)))
                for key in floor['keys']:
                    self.assertRegex(key[3][0], r'^ff[0-9a-f]{6}$')

    def test_nxe_color_group_now_decodes(self):
        result = decode(self.scene('NXE/Retail/9199'))
        self.assertTrue(result['verified'])
        self.assertEqual(len(result['group']['timelines']['named']), 17)

    def test_unresolved_scenes_fail_closed(self):
        for source in ('Metro/V2/Retail/17559',):
            with self.subTest(source=source):
                self.assertFalse(decode(self.scene(source))['verified'])

    def test_truncation_is_not_a_palette(self):
        data = self.scene('Kinect/Retail/13604')
        with self.assertRaises((AssertionError, ValueError, IndexError)):
            decode(data[:-1])


if __name__ == '__main__':
    unittest.main()
