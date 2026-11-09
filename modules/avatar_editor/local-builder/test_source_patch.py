from pathlib import Path
import tempfile
import unittest
import source_patch as port


class SourcePatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / 'user files'
        self.output = self.root / 'generated'
        self.source.mkdir()
        self.output.mkdir()
        self.original = b'original fixture\r\n'
        (self.source / 'source.cpp').write_bytes(self.original)
        self.recipe = dict(schema=1, inputs={'base': dict(root='title', path='source.cpp',
            sha256=port.sha256(self.original))}, operations=[dict(copy='base', offset=0,
            length=len(self.original)), dict(authored_utf8='// authored addition\n')],
            output='src/source.cpp', output_sha256=port.sha256(
                self.original + b'// authored addition\n'))

    def tearDown(self):
        self.assertEqual((self.source / 'source.cpp').read_bytes(), self.original)
        self.temp.cleanup()

    def apply(self):
        return port.apply(self.recipe, {'title': self.source}, self.output)

    def test_exact_reconstruction_preserves_original(self):
        result = self.apply()
        self.assertEqual(result['sha256'], self.recipe['output_sha256'])
        self.assertEqual((self.output / 'src/source.cpp').read_bytes(),
                         self.original + b'// authored addition\n')

    def test_different_input_rejected_before_output(self):
        self.recipe['inputs']['base']['sha256'] = '0' * 64
        with self.assertRaisesRegex(port.PatchError, 'Unsupported source version'):
            self.apply()
        self.assertFalse((self.output / 'src').exists())

    def test_wrong_result_rejected_before_output(self):
        self.recipe['output_sha256'] = '0' * 64
        with self.assertRaisesRegex(port.PatchError, 'Reconstructed'):
            self.apply()
        self.assertFalse((self.output / 'src').exists())

    def test_out_of_bounds_copy_rejected(self):
        self.recipe['operations'][0]['length'] += 1
        with self.assertRaisesRegex(port.PatchError, 'copy range'):
            self.apply()

    def test_escape_output_rejected(self):
        for path in ['../source.cpp', '/source.cpp', 'C:/source.cpp', 'a\\source.cpp']:
            self.recipe['output'] = path
            with self.assertRaises(port.PatchError):
                self.apply()

    def test_cannot_overwrite_existing_generated_source(self):
        self.apply()
        with self.assertRaises(FileExistsError):
            self.apply()

    def test_cannot_write_into_originals(self):
        with self.assertRaisesRegex(port.PatchError, 'separate'):
            port.apply(self.recipe, {'title': self.source}, self.source)


if __name__ == '__main__':
    unittest.main()
