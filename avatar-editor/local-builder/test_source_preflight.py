"""Original fixture tests for source preparation conflict checks."""
from pathlib import Path
import hashlib
import json
import tempfile
import unittest

from source_patch import PatchError
from source_preflight import coverage, merge_manifests, verify_staged


class SourcePreflightTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.a = hashlib.sha256(b'original fixture a').hexdigest()
        self.b = hashlib.sha256(b'original fixture b').hexdigest()

    def manifest(self, name, expected):
        value = {'schema': 1, 'copies': [{'root': 'supplied', 'path': 'fixture.h',
                 'input_sha256': [expected], 'output': 'source/fixture.h', 'output_sha256': expected}]}
        (self.root / name).write_text(json.dumps(value))
        return name

    def test_conflicting_sources_fail_without_explicit_hash_override(self):
        names = [self.manifest('a.json', self.a), self.manifest('b.json', self.b)]
        with self.assertRaisesRegex(PatchError, 'Conflicting source destination'):
            merge_manifests(self.root, names)
        merged, report = merge_manifests(self.root, names, {'source/fixture.h': self.a})
        self.assertEqual(merged['copies'][0]['output_sha256'], self.a)
        self.assertEqual(report['superseded'][0]['dropped_sha256'], self.b)

    def test_same_hash_deduplicates_but_missing_recipe_is_not_a_provider(self):
        names = [self.manifest('a.json', self.a), self.manifest('b.json', self.a)]
        merged, report = merge_manifests(self.root, names)
        self.assertEqual(len(merged['copies']), 1)
        required = [{'destination': 'source/fixture.h', 'sha256': self.a},
                    {'destination': 'source/missing.h', 'sha256': self.b}]
        result = coverage(required, report['providers'])
        self.assertEqual(result['status'], 'INCOMPLETE')
        self.assertEqual([item['destination'] for item in result['missing']], ['source/missing.h'])
        self.assertFalse(result['package_ready'])

    def test_producer_cannot_silently_replace_different_source_hash(self):
        provider = {'destination': 'source/fixture.h', 'sha256': self.a}
        with self.assertRaisesRegex(PatchError, 'Producer/source output conflict'):
            coverage([provider], [provider], [{'destination': provider['destination'], 'sha256': self.b}])

    def test_staged_verification_rejects_tamper_and_does_not_modify_file(self):
        source = self.root / 'source/fixture.h'
        source.parent.mkdir()
        source.write_bytes(b'original fixture a')
        required = [{'destination': 'source/fixture.h', 'sha256': self.a}]
        self.assertEqual(verify_staged(required, self.root)['status'], 'PASS')
        source.write_bytes(b'tampered fixture')
        self.assertEqual(verify_staged(required, self.root)['status'], 'FAIL')
        self.assertEqual(source.read_bytes(), b'tampered fixture')


if __name__ == '__main__':
    unittest.main(verbosity=2)
