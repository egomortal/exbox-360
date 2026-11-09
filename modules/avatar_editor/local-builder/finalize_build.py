"""Finish a fresh local build without installation or console access.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import traceback

import companion_producer
import splash_producer
import dependency_aliases
import native_link
import native_verify
import package_editor
import filter_behavior
import splash_behavior
from source_patch import relative_file


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def behavior_guard(run, app):
    receipt = run / 'behavior/focused-behavior-result.json'
    proof = read(app / 'behavior-source-proof.json')
    result = read(receipt)
    if (result.get('status') != 'PASS' or result.get('focused_execution') is not True
            or result.get('source_sha256') != proof['source_sha256']
            or result.get('source_proof_sha256') != native_verify.digest(app / 'behavior-source-proof.json')
            or result.get('harness_sha256') != proof['focused_harness_sha256']
            or result.get('previous_full_application_receipt_sha256') !=
            proof['previous_full_application_receipt_sha256']):
        raise ValueError('Mandatory focused behavior receipt is missing or differs')
    source = run / 'source/tryon/rev2'
    for name, expected in proof['source_sha256'].items():
        if native_verify.digest(relative_file(source, name)) != expected:
            raise ValueError('Production try-on source changed: ' + name)
    return receipt, source


def execute(action, inputs, run):
    app = Path(__file__).resolve().parent
    run = Path(run).resolve(strict=True)
    if inputs.get('schema') != 1:
        raise ValueError('Expected workflow input schema 1')
    inputs = dependency_aliases.for_run(inputs, run)
    dependencies = Path(inputs['dependencies']).resolve(strict=True)
    receipts = run / 'workflow-receipts'
    receipts.mkdir(exist_ok=True)
    receipt = receipts / (action + '.json')
    failure = receipts / (action + '-failed.json')
    if receipt.exists() or failure.exists():
        raise ValueError('Stage already attempted; preserve its receipts and use a fresh run')
    try:
        if action == 'link':
            result = native_link.link(run, dependencies, app)
        elif action == 'verify':
            linked = read(run / 'native/link-logs/result.json')
            llvm = run / 'native/linked/llvm-pie.elf'
            native = run / 'native/linked/frontend-native.elf'
            if (linked.get('status') != 'PASS' or linked.get('llvm_elf_sha256') != native_verify.digest(llvm)
                    or linked.get('native_elf_sha256') != native_verify.digest(native)):
                raise ValueError('Fresh link receipt does not describe these files')
            graph = read(app / 'native-plan.json')
            adapters = [u for u in graph['units'] if u['source'] == 'source/tryon/rev2/adapter.cpp']
            if len(adapters) != 1:
                raise ValueError('Expected one production try-on adapter')
            result = native_verify.verify(llvm, native, relative_file(run, adapters[0]['output']),
                run / 'native/linked/link.map', read(app / 'recipes/native-imports.json'),
                dependencies / 'llvm21/bin/llvm-nm', dependencies / 'llvm21/bin/llvm-readelf',
                run / 'native/verification')
        elif action == 'companions':
            extracted = read(run / 'ui-extraction/result.json')
            assets = Path(extracted['ui_directory']).resolve(strict=True)
            if extracted.get('status') != 'PASS' or not assets.is_relative_to(run / 'ui-extraction'):
                raise ValueError('Expected fresh UI extraction inside this run')
            destination = run / 'companions'
            if destination.exists():
                raise ValueError('Companion output must be fresh')
            libc = companion_producer.build_libc(inputs['sources']['vulkan-port-source'],
                inputs['tools']['host_cxx'], dependencies / 'native-tools/ps5-native-tool',
                run / 'companion-runtime')
            files = companion_producer.generate_from_originals(assets, inputs['originals']['xtt'], libc)
            splashes = splash_producer.generate(Path(inputs['originals']['splash']))
            if set(files) & set(splashes):
                raise ValueError('Splash output overlaps baseline companion')
            files.update(splashes)
            if {name: hashlib.sha256(data).hexdigest() for name, data in files.items()} != package_editor.expected_companions():
                raise ValueError('Generated companion hashes differ')
            destination.mkdir()
            for name, data in files.items():
                path = relative_file(destination, name)
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open('xb') as stream:
                    stream.write(data)
            result = dict(status='PASS', companions={name: native_verify.digest(relative_file(destination, name))
                for name in files}, fresh_runtime=True, original_assets_embedded_in_app=False,
                splash_input_sha256=splash_producer.INPUT_SHA256, splash_generated_locally=True)
        elif action == 'package':
            filter_behavior.guard(run)
            splash_behavior.guard(run)
            behavior, source = behavior_guard(run, app)
            companions = read(receipts / 'companions.json')
            if (companions.get('status') != 'PASS' or
                    companions.get('companions') != package_editor.expected_companions() or
                    companions.get('splash_input_sha256') != splash_producer.INPUT_SHA256 or
                    companions.get('splash_generated_locally') is not True):
                raise ValueError('Fresh companion production has not passed')
            result = package_editor.package(run / 'native/linked/frontend-native.elf',
                run / 'native/verification/native-verification.json', run / 'companions',
                companions['companions'], dependencies / 'native-tools/ps5-native-tool', run / 'package',
                behavior_receipt=behavior, source_dir=source)
        else:
            raise ValueError('Unknown finalization action')
        with receipt.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2)
            stream.write('\n')
        return result
    except BaseException as error:
        with failure.open('x', encoding='utf-8') as stream:
            json.dump(dict(status='FAIL', action=action, error=str(error), traceback=traceback.format_exc()), stream, indent=2)
            stream.write('\n')
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action', choices=['link', 'verify', 'companions', 'package'], required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--run', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(execute(args.action, read(args.inputs), args.run), indent=2), flush=True)


if __name__ == '__main__':
    main()
