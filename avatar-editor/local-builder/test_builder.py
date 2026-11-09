import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from unittest.mock import Mock

import builder


class LocalBuilderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.app = self.root / 'app'
        self.inputs = self.root / 'my originals'
        self.deps = self.root / 'dependencies'
        self.work = self.root / 'work'
        for path in [self.app, self.inputs, self.deps]:
            path.mkdir()
        self.original = self.inputs / 'original.dat'
        self.original.write_bytes(b'user-owned test fixture')
        self.before = builder.digest(self.original)
        self.patch = patch.object(builder, 'ROOT', self.app)
        self.patch.start()

    def tearDown(self):
        self.assertEqual(builder.digest(self.original), self.before)
        self.patch.stop()
        self.temp.cleanup()

    def recipe(self, **changes):
        value = dict(schema=1, release_ready=False, required_inputs=[], stages=[])
        value.update(changes)
        (self.app / 'pipeline.json').write_text(json.dumps(value))

    def run_build(self):
        with contextlib.redirect_stdout(io.StringIO()):
            builder.build(self.inputs, self.deps, self.work)

    def test_development_pipeline_never_creates_work_or_claims_success(self):
        self.recipe()
        with self.assertRaisesRegex(builder.BuildError, 'Development preview'):
            self.run_build()
        self.assertFalse(self.work.exists())

    def test_output_cannot_replace_originals(self):
        for output in [self.inputs, self.inputs / 'output', self.root]:
            with self.assertRaises(builder.BuildError):
                builder.validate_paths(self.inputs, self.deps, output)

    def test_user_inputs_cannot_be_under_public_checkout(self):
        private = self.app / 'private'
        private.mkdir()
        with self.assertRaises(builder.BuildError):
            builder.validate_paths(private, self.deps, self.work)

    def test_symlink_output_cannot_alias_originals(self):
        alias = self.root / 'alias'
        try:
            alias.symlink_to(self.inputs, target_is_directory=True)
        except OSError:
            self.skipTest('Symlink creation is unavailable')
        with self.assertRaises(builder.BuildError):
            builder.validate_paths(self.inputs, self.deps, alias)

    def test_source_to_package_stages_are_required(self):
        self.recipe(release_ready=True)
        with self.assertRaisesRegex(builder.BuildError, 'Incomplete native'):
            self.run_build()
        self.assertFalse(self.work.exists())

    def test_failed_stage_preserves_log_and_never_starts_next_stage(self):
        stages = [dict(kind=kind, label=kind, argv=[sys.executable, '-c', 'raise SystemExit(7)'])
                  for kind in ['prepare', 'generate', 'compile', 'link', 'verify', 'package']]
        self.recipe(release_ready=True, stages=stages, package='PPSA99734/eboot.bin')
        with self.assertRaisesRegex(builder.BuildError, 'prepare failed'):
            self.run_build()
        runs = list(self.work.iterdir())
        self.assertEqual(len(runs), 1)
        result = json.loads((runs[0] / 'result.json').read_text())
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(result['stages'], [{'label': 'prepare', 'exit_code': 7}])
        self.assertTrue((runs[0] / '01.log').exists())
        self.assertFalse((runs[0] / '02.log').exists())

    def test_input_escape_is_rejected_before_commands(self):
        stages = [dict(kind=kind, label=kind, argv=['never-run'])
                  for kind in ['prepare', 'generate', 'compile', 'link', 'verify', 'package']]
        self.recipe(release_ready=True, stages=stages, required_inputs=[dict(
            root='inputs', path='../dependencies', label='fixture', sha256='0' * 64)])
        with self.assertRaisesRegex(builder.BuildError, 'Missing required input'):
            self.run_build()
        self.assertFalse(self.work.exists())

    def test_shipped_pipeline_uses_one_nested_workspace(self):
        # Inert subprocess fixtures exercise the real orchestration recipe.
        # They do not validate or execute native stages.
        recipe = json.loads(Path(__file__).with_name('pipeline.json').read_text())
        recipe.update(release_ready=True, required_inputs=[])
        (self.app / 'pipeline.json').write_text(json.dumps(recipe))
        calls = []

        def fake_process(argv, **kwargs):
            calls.append(argv)
            self.assertFalse(any('${' in arg for arg in argv))
            outer = Path(kwargs['cwd'])
            nested = outer / 'native-work'
            if '--run' in argv:
                self.assertEqual(argv[argv.index('--run') + 1], str(nested))
            if '--inputs' in argv:
                self.assertEqual(argv[argv.index('--inputs') + 1], str(outer / 'inputs.json'))
            if str(self.app / 'focused_behavior.py') in argv:
                self.assertEqual(argv[argv.index('--project') + 1], str(nested / 'source/project'))
                self.assertEqual(argv[argv.index('--output') + 1], str(nested / 'behavior'))
                self.assertEqual(argv[argv.index('--sdk') + 1], str(self.deps / 'sources/rexglue_sdk'))
            if '--action' in argv and argv[argv.index('--action') + 1] == 'package':
                package = outer / recipe['package']
                package.parent.mkdir(parents=True)
                package.write_bytes(b'synthetic orchestration fixture only')
            return Mock(stdout=io.StringIO('synthetic stage\n'), wait=Mock(return_value=0))

        with patch.object(builder.subprocess, 'Popen', side_effect=fake_process):
            self.run_build()
        self.assertEqual(len(calls), len(recipe['stages']))
        result = json.loads(next(self.work.glob('*/result.json')).read_text())
        self.assertEqual(result['status'], 'PASS')
        self.assertTrue(result['package'].endswith('native-work/package/Editor-125ms-Sort-Filter-Splash.zip'))


if __name__ == '__main__':
    unittest.main()
