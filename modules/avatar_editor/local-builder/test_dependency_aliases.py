"""Original fixtures only; no supplied tool or editor compilation."""
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from dependency_aliases import prepare, ROLES


class AliasTests(unittest.TestCase):
    def test_spaces_wrapper_and_reuse(self):
        with tempfile.TemporaryDirectory(prefix='input aliases ') as temporary:
            base = Path(temporary)
            for role in ROLES:
                (base / role).mkdir()
            scripts = base / 'payload-sdk'
            helper = scripts / 'helper'
            helper.write_text('#!/bin/sh\nprintf fixture-pass')
            helper.chmod(0o700)
            wrapper = scripts / 'wrapper'
            wrapper.write_text('#!/bin/bash\nSCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"\n$SCRIPT_DIR/helper\n')
            wrapper.chmod(0o700)
            before = wrapper.read_bytes()
            inputs = {'dependencies': str(base), 'tools': {'shader_tools': str(base / 'host-tools')}}
            receipt = base / 'private-receipt.json'
            effective = prepare(inputs, receipt)
            root = Path(effective['dependencies'])
            self.assertFalse(any(c.isspace() for c in str(root)))
            self.assertEqual(subprocess.check_output([str(root / 'payload-sdk/wrapper')]), b'fixture-pass')
            self.assertEqual(wrapper.read_bytes(), before)
            self.assertEqual(prepare(inputs, receipt), effective)
            self.assertEqual(prepare(effective, receipt), effective)
            self.assertEqual(json.loads(receipt.read_text())['status'], 'PASS')
            with self.assertRaises(ValueError):
                prepare(dict(inputs, extra='different'), receipt)
            alias = root / 'dxc'
            alias.unlink()
            alias.symlink_to(base / 'llvm21', target_is_directory=True)
            with self.assertRaises(ValueError):
                prepare(inputs, receipt)
            for path in root.iterdir():
                path.unlink()
            root.rmdir()

    def test_no_space_path_unchanged(self):
        with tempfile.TemporaryDirectory(prefix='inputs-') as temporary:
            inputs = {'dependencies': temporary}
            receipt = Path(temporary) / 'unused.json'
            self.assertEqual(prepare(inputs, receipt), inputs)
            self.assertFalse(receipt.exists())


if __name__ == '__main__':
    unittest.main()
