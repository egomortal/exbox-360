import json
from pathlib import Path
import unittest
import companion_producer
class CompanionContractTests(unittest.TestCase):
    def test_inputs_stay_in_their_declared_roles(self):
        contract=json.loads(Path(__file__).with_name('companion-inputs.json').read_text())
        self.assertEqual(companion_producer.LIBC_INPUTS,contract['input_roles']['vulkan_source']['files'])
        self.assertEqual(companion_producer.INPUTS,contract['input_roles']['ui_pngs']['files'])
        self.assertFalse(set(companion_producer.LIBC_INPUTS)&set(companion_producer.INPUTS))
        self.assertEqual(companion_producer.EXPECTED,contract['outputs'])


class SplashPackageContractTests(unittest.TestCase):
    def test_declared_source_and_output_contract(self):
        import splash_producer
        import package_editor
        recipe = json.loads(Path(__file__).with_name('pipeline.json').read_text())
        inputs = [item for item in recipe['required_inputs'] if item['path'] == 'splash_360.png']
        self.assertEqual(len(inputs), 1)
        self.assertEqual(inputs[0]['sha256'], splash_producer.INPUT_SHA256)
        self.assertEqual(set(splash_producer.EXPECTED), {
            'assets/ui/loading-1080p.png', 'assets/ui/loading-2160p.png'})
        self.assertEqual(len(package_editor.expected_companions()), 8)
        self.assertEqual(recipe['package'], 'native-work/package/Editor-125ms-Sort-Filter-Splash.zip')

    def test_package_rejects_missing_extra_and_changed_splash(self):
        import hashlib
        import tempfile
        from unittest.mock import patch
        import package_editor
        import splash_producer
        # Original fixture bytes only: no user artwork is embedded in tests.
        expected = {name: hashlib.sha256(name.encode()).hexdigest()
                    for name in package_editor.expected_companions()}
        base = {k: expected[k] for k in companion_producer.EXPECTED}
        splash = {k: expected[k] for k in splash_producer.EXPECTED}
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(companion_producer, 'EXPECTED', base), \
                patch.object(splash_producer, 'EXPECTED', splash):
            root = Path(directory)
            for name in expected:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            self.assertEqual(package_editor.verify_companions(root, expected), set(expected))
            missing = dict(expected)
            del missing['assets/ui/loading-1080p.png']
            with self.assertRaisesRegex(ValueError, 'two generated splash'):
                package_editor.verify_companions(root, missing)
            with self.assertRaisesRegex(ValueError, 'two generated splash'):
                package_editor.verify_companions(root, {**expected, 'extra.png': '0' * 64})
            (root / 'assets/ui/loading-1080p.png').write_bytes(b'changed image')
            with self.assertRaisesRegex(ValueError, 'Companion version differs'):
                package_editor.verify_companions(root, expected)
            # A forged receipt cannot authorize the changed bytes.
            forged = dict(expected)
            forged['assets/ui/loading-1080p.png'] = hashlib.sha256(b'changed image').hexdigest()
            with self.assertRaisesRegex(ValueError, 'two generated splash'):
                package_editor.verify_companions(root, forged)


class SplashAdmissionTests(unittest.TestCase):
    def test_splash_stage_follows_generation_and_precedes_package(self):
        recipe = json.loads(Path(__file__).with_name('pipeline.json').read_text())
        stages = recipe['stages']
        generation = next(i for i, s in enumerate(stages) if 'companions' in s['argv'])
        gate = next(i for i, s in enumerate(stages) if '${app}/splash_behavior.py' in s['argv'])
        packaging = next(i for i, s in enumerate(stages) if s['kind'] == 'package')
        self.assertLess(generation, gate)
        self.assertLess(gate, packaging)

    def test_missing_failed_and_stale_splash_receipts_are_rejected(self):
        import tempfile
        from unittest.mock import patch
        import splash_behavior
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, run = root / 'app', root / 'run'
            app.mkdir(); run.mkdir()
            image = run / 'fixture.png'
            image.write_bytes(b'original synthetic fixture')
            contract = {'sources': {}, 'tests': {}, 'payloads': ['fixture.png']}
            contract_file = app / 'splash-contract.json'
            contract_file.write_text(json.dumps(contract))
            with patch.object(splash_behavior, 'APP', app):
                with self.assertRaises(FileNotFoundError):
                    splash_behavior.guard(run)
                (run / 'splash-behavior').mkdir()
                receipt = run / 'splash-behavior/result.json'
                result = {'status': 'FAIL', 'host_execution': True,
                          'actual_payload_execution': True,
                          'contract_sha256': splash_behavior.digest(contract_file),
                          'sources': {}, 'tests': {},
                          'payloads': {'fixture.png': splash_behavior.digest(image)}}
                receipt.write_text(json.dumps(result))
                with self.assertRaisesRegex(ValueError, 'gate has not passed'):
                    splash_behavior.guard(run)
                result['status'] = 'PASS'
                receipt.write_text(json.dumps(result))
                self.assertEqual(splash_behavior.guard(run)['status'], 'PASS')
                image.write_bytes(b'changed after receipt')
                with self.assertRaisesRegex(ValueError, 'gate has not passed'):
                    splash_behavior.guard(run)
                image.write_bytes(b'original synthetic fixture')
                result['actual_payload_execution'] = False
                receipt.write_text(json.dumps(result))
                with self.assertRaisesRegex(ValueError, 'gate has not passed'):
                    splash_behavior.guard(run)
