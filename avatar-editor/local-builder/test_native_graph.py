"""Host-only source graph checks using original tiny C fixtures.

SPDX-License-Identifier: GPL-3.0-or-later
"""
from copy import deepcopy
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

import native_graph


class NativeGraphTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='editor-graph-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.run = self.root / 'run'
        self.dependencies = self.root / 'dependencies'
        self.app = self.root / 'app'
        for path in (self.run, self.dependencies, self.app):
            path.mkdir()
        self.cc = shutil.which('cc')
        self.ar = shutil.which('ar')
        self.nm = shutil.which('nm')
        if not all((self.cc, self.ar, self.nm)):
            self.skipTest('Host C compiler, ar and nm are required')
        self.plan = {'schema': 1, 'units': [], 'archives': []}
        for label in ('first', 'second'):
            source = self.run / f'{label}.c'
            source.write_text(f'int graph_fixture_{label}(void) {{ return 17; }}\n')
            output = f'objects/{label}/same_object_name.o'
            self.plan['units'].append({
                'id': label, 'source': source.name,
                'source_sha256': native_graph.digest(source), 'output': output,
                'optimization': '-O2',
                'argv': [self.cc, '-O2', '-c', '${run}/' + source.name,
                         '-o', '${run}/' + output],
            })
        self.plan['archives'].append({
            'path': 'libfixture.a',
            'members': [{'unit': label, 'name': 'same_very_long_archive_member_name.o'}
                        for label in ('first', 'second')],
            'index_argv': [self.ar, 's', '${archive}'],
        })

    def execute(self, plan=None):
        return native_graph.execute(plan or self.plan, self.run, self.dependencies, self.app)

    def assert_no_compile(self):
        self.assertFalse((self.run / 'native-logs').exists())
        self.assertFalse((self.run / 'objects').exists())
        self.assertFalse((self.run / 'libfixture.a').exists())

    def test_duplicate_long_archive_names_preserve_both_symbols(self):
        receipt = self.execute()
        archive = self.run / 'libfixture.a'
        names = subprocess.check_output([self.ar, 't', str(archive)], text=True).splitlines()
        self.assertEqual(names, ['same_very_long_archive_member_name.o'] * 2)
        symbols = subprocess.check_output([self.nm, '--defined-only', str(archive)], text=True)
        self.assertEqual(symbols.count(' graph_fixture_first\n'), 1)
        self.assertEqual(symbols.count(' graph_fixture_second\n'), 1)
        self.assertEqual(receipt['status'], 'PASS')
        self.assertEqual(len(receipt['units']), 2)
        self.assertTrue((self.run / 'native-graph-result.json').is_file())

    def test_objects_do_not_depend_on_the_build_folder(self):
        objects = []
        for name in ('first-folder', 'a-much-longer-second-build-folder'):
            run = self.root / name / 'run'
            run.mkdir(parents=True)
            source = run / 'where.c'
            source.write_text('const char *graph_fixture_where(void) { return __FILE__; }\n'
                              'const char *graph_fixture_home(void) { return AE_SOURCE_DIR; }\n')
            plan = {'schema': 1, 'archives': [], 'units': [{
                'id': 'where', 'source': 'where.c', 'source_sha256': native_graph.digest(source),
                'output': 'objects/where.o', 'optimization': '-O2',
                'argv': [self.cc, '-O2', '-g', '-DAE_SOURCE_DIR="${run}/data"', '-c', '${run}/where.c', '-o', '${run}/objects/where.o']}]}
            native_graph.execute(plan, run, self.dependencies, self.app)
            objects.append((run / 'objects/where.o').read_bytes())
        self.assertEqual(objects[0], objects[1])
        self.assertIn(native_graph.STABLE_RUN.encode(), objects[0])
        self.assertNotIn(str(self.root).encode(), objects[0])

    def test_later_source_hash_mismatch_prevents_all_compiles(self):
        (self.run / 'second.c').write_text('int graph_fixture_changed(void) { return 99; }\n')
        with self.assertRaisesRegex(native_graph.GraphError, 'Source version differs: second'):
            self.execute()
        self.assert_no_compile()

    def test_added_O3_is_rejected_when_O2_is_required(self):
        plan = deepcopy(self.plan)
        plan['units'][1]['argv'].append('-O3')
        with self.assertRaisesRegex(native_graph.GraphError, 'Effective optimization differs: second'):
            self.execute(plan)
        self.assert_no_compile()

    def test_prebuilt_member_cannot_replace_source_unit(self):
        prebuilt = self.dependencies / 'private-prebuilt.o'
        prebuilt.write_bytes(b'not an input to a source build')
        plan = deepcopy(self.plan)
        plan['archives'][0]['members'][1]['unit'] = str(prebuilt)
        with self.assertRaisesRegex(native_graph.GraphError, 'Archive requires a source-built member'):
            self.execute(plan)
        self.assert_no_compile()
        self.assertEqual(prebuilt.read_bytes(), b'not an input to a source build')

    def test_two_compilers_overlap_and_receipt_keeps_graph_order(self):
        barrier = threading.Barrier(2, timeout=10)
        original_run = subprocess.run

        def overlapping_run(argv, **kwargs):
            if '-c' in argv:
                barrier.wait()
            return original_run(argv, **kwargs)

        with patch.object(native_graph.subprocess, 'run', side_effect=overlapping_run):
            receipt = self.execute()
        self.assertEqual([item['id'] for item in receipt['units']], ['first', 'second'])
        self.assertEqual(len(list((self.run / 'native-logs').glob('*.log'))), 2)

    def test_relative_compiler_paths_use_declared_working_directory(self):
        plan = deepcopy(self.plan)
        (self.run / 'build').mkdir()
        for unit in plan['units']:
            unit['cwd'] = 'build'
            unit['argv'][unit['argv'].index('-c') + 1] = '../' + unit['source']
            unit['argv'][unit['argv'].index('-o') + 1] = '../' + unit['output']
        receipt = self.execute(plan)
        self.assertEqual(receipt['status'], 'PASS')
        self.assertTrue((self.run / 'libfixture.a').is_file())

    def test_relative_output_outside_declared_target_is_rejected(self):
        plan = deepcopy(self.plan)
        (self.run / 'build').mkdir()
        plan['units'][0]['cwd'] = 'build'
        plan['units'][0]['argv'][-1] = '../../dependencies/original.o'
        with self.assertRaisesRegex(native_graph.GraphError, 'Compiler output differs'):
            self.execute(plan)
        self.assert_no_compile()
        self.assertFalse((self.dependencies / 'original.o').exists())

    def test_compile_failure_preserves_logs_and_stops_later_units_and_archives(self):
        plan = deepcopy(self.plan)
        third = deepcopy(plan['units'][1])
        third.update(id='third', output='objects/third.o')
        third['argv'][-1] = '${run}/objects/third.o'
        plan['units'].append(third)
        barrier = threading.Barrier(2, timeout=10)
        original_run = subprocess.run

        def failing_run(argv, **kwargs):
            if '-c' in argv:
                barrier.wait()
                if argv[argv.index('-c') + 1].endswith('/first.c'):
                    kwargs['stdout'].write(b'fixture compiler failure\n')
                    raise subprocess.CalledProcessError(23, argv)
            return original_run(argv, **kwargs)

        with patch.object(native_graph.subprocess, 'run', side_effect=failing_run):
            with self.assertRaises(subprocess.CalledProcessError) as failure:
                self.execute(plan)
        self.assertEqual(failure.exception.returncode, 23)
        self.assertIn('fixture compiler failure', (self.run / 'native-logs/0001.log').read_text())
        self.assertTrue((self.run / plan['units'][1]['output']).is_file())
        self.assertFalse((self.run / third['output']).exists())
        self.assertFalse((self.run / 'native-logs/0003.log').exists())
        self.assertFalse((self.run / 'libfixture.a').exists())
        self.assertFalse((self.run / 'native-graph-result.json').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
