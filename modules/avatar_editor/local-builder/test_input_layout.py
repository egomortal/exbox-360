import json
from pathlib import Path
import tempfile
import unittest
import input_layout


class InputLayoutTests(unittest.TestCase):
    def test_missing_inputs_never_create_map(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            originals, deps = root / 'originals', root / 'deps'
            originals.mkdir()
            deps.mkdir()
            output = root / 'run/inputs.json'
            with self.assertRaisesRegex(ValueError, 'Missing supplied inputs'):
                input_layout.resolve(originals, deps, output)
            self.assertFalse(output.exists())

    def test_complete_layout_preserves_inputs_and_compiler_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            originals, deps = root / 'originals', root / 'deps'
            originals.mkdir()
            for name in ('avatareditor.xex', 'xenonclatin.xtt', 'splash_360.png'):
                (originals / name).write_bytes(b'fixture only')
            for role in input_layout.SOURCE_ROLES + ('xex-tool',):
                (deps / 'sources' / role).mkdir(parents=True)
            for name in ('native-sdk', 'host-tools', 'llvm21/bin'):
                (deps / name).mkdir(parents=True)
            (deps / 'host-tools/rexglue').write_bytes(b'tool fixture')
            clang = deps / 'llvm21/bin/clang'
            clang.write_bytes(b'tool fixture')
            (deps / 'llvm21/bin/clang++').symlink_to(clang)
            output = root / 'run/inputs.json'
            result = input_layout.resolve(originals, deps, output)
            self.assertEqual(result, json.loads(output.read_text()))
            self.assertEqual(Path(result['originals']['splash']), originals / 'splash_360.png')
            self.assertTrue(result['tools']['host_cxx'].endswith('/clang++'))
            self.assertEqual((originals / 'avatareditor.xex').read_bytes(), b'fixture only')
            with self.assertRaises(FileExistsError):
                input_layout.resolve(originals, deps, output)
            with self.assertRaisesRegex(ValueError, 'outside originals'):
                input_layout.resolve(originals, deps, originals / 'inputs.json')
            (originals / 'splash_360.png').unlink()
            missing_output = root / 'missing-splash/inputs.json'
            with self.assertRaisesRegex(ValueError, 'splash_360.png'):
                input_layout.resolve(originals, deps, missing_output)
            self.assertFalse(missing_output.exists())


if __name__ == '__main__':
    unittest.main()
