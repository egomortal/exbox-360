"""Original fixtures for the retained lexical include-path portability fix."""
from pathlib import Path
import tempfile
import unittest
from workflow import prepare_include_directories


class IncludePathTests(unittest.TestCase):
    def test_retained_parent_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            run = Path(temporary)
            (run / 'headers').mkdir()
            header = run / 'headers/example.h'
            header.write_bytes(b'// original test fixture\n')
            argv = ['cc', '-I${run}/modules/missing/../../headers']
            graph = {'units': [{'argv': list(argv)}]}
            result = prepare_include_directories(run, graph, {'native_compile_argv': []})
            self.assertEqual(graph['units'][0]['argv'], argv)
            self.assertTrue((run / 'modules/missing/../../headers/example.h').is_file())
            self.assertEqual(header.read_bytes(), b'// original test fixture\n')
            self.assertIn('modules/missing', result['created'])
            self.assertFalse(result['compiler_flags_changed'])
            self.assertEqual(prepare_include_directories(run, graph, {'native_compile_argv': []})['created'], [])

    def test_escape_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                prepare_include_directories(Path(temporary),
                    {'units': [{'argv': ['cc', '-I${run}/../outside']}]},
                    {'native_compile_argv': []})


if __name__ == '__main__':
    unittest.main()
