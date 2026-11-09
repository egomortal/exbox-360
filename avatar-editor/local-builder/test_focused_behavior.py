"""Input-preservation checks for the host-only focused gate."""
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

import focused_behavior


class FocusedBehaviorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / 'project'
        self.project.mkdir()
        self.source = self.root / 'tryon'
        shutil.copytree(Path(focused_behavior.__file__).parent / 'tryon', self.source)
        self.compiler = self.root / 'compiler'
        self.compiler.write_bytes(b'not executed in preflight tests')
        self.output = self.root / 'result'

    def test_changed_adapter_stops_before_compilation_or_output_creation(self):
        original = (self.source / 'adapter.cpp').read_bytes()
        (self.source / 'adapter.cpp').write_bytes(original + b'\n')
        with patch.object(focused_behavior.subprocess, 'run') as child:
            with self.assertRaisesRegex(ValueError, 'source or harness differs: adapter.cpp'):
                focused_behavior.run(self.project, self.compiler, self.output, self.source)
        child.assert_not_called()
        self.assertFalse(self.output.exists())
        self.assertEqual((self.source / 'adapter.cpp').read_bytes(), original + b'\n')

    def test_output_cannot_overlap_original_sources(self):
        with patch.object(focused_behavior.subprocess, 'run') as child:
            with self.assertRaisesRegex(ValueError, 'separate from supplied sources'):
                focused_behavior.run(self.project, self.compiler, self.source / 'result', self.source)
        child.assert_not_called()
        self.assertFalse((self.source / 'result').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
