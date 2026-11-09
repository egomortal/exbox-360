"""Create a local editor package after native verification. No installation.

SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import json
import shutil
import subprocess
import zipfile

from native_verify import digest, layout
import filter_behavior
import splash_behavior
import companion_producer
import splash_producer
from source_patch import relative_file


def expected_companions():
    if set(companion_producer.EXPECTED) & set(splash_producer.EXPECTED):
        raise ValueError('Splash output overlaps baseline companion')
    return {**companion_producer.EXPECTED, **splash_producer.EXPECTED}


def verify_companions(companion_root, companions):
    expected = expected_companions()
    if companions != expected:
        raise ValueError('Expected six existing companions and two generated splash images')
    for name, fingerprint in expected.items():
        if digest(relative_file(companion_root, name)) != fingerprint:
            raise ValueError('Companion version differs: ' + name)
    return set(expected)


def package(native_elf, verification, companion_root, companions, tool, output,
            *, behavior_receipt, source_dir):
    native_elf = Path(native_elf).resolve(strict=True)
    filter_result = filter_behavior.guard(native_elf.parent.parent.parent)
    splash_result = splash_behavior.guard(native_elf.parent.parent.parent)
    verification = Path(verification).resolve(strict=True)
    companion_root = Path(companion_root).resolve(strict=True)
    output = Path(output).resolve()
    behavior_receipt = Path(behavior_receipt).resolve(strict=True)
    source_dir = Path(source_dir).resolve(strict=True)
    source_proof = json.loads(Path(__file__).with_name('behavior-source-proof.json').read_text())
    behavior = json.loads(behavior_receipt.read_text())
    if (behavior.get('status') != 'PASS' or behavior.get('focused_execution') is not True or
            behavior.get('source_sha256') != source_proof['source_sha256'] or
            behavior.get('previous_full_application_receipt_sha256') !=
            source_proof['previous_full_application_receipt_sha256']):
        raise ValueError('Focused behavior and source validation have not passed')
    for name, expected in source_proof['source_sha256'].items():
        if digest(relative_file(source_dir, name)) != expected:
            raise ValueError('Production try-on source differs: ' + name)
    proof = json.loads(verification.read_text())
    if (proof.get('status') != 'PASS' or proof.get('imports') != 441 or
            proof.get('limits_MiB') != [71, 26, 2, 25, 3] or
            proof.get('native_elf_sha256') != digest(native_elf)):
        raise ValueError('Native verification does not describe this editor')
    layout(native_elf)
    required = verify_companions(companion_root, companions)
    if output.exists() or output.is_relative_to(companion_root) or companion_root.is_relative_to(output):
        raise ValueError('Package output must be fresh and separate from inputs')
    output.mkdir(parents=True, exist_ok=False)
    app = output / 'PPSA99734'
    app.mkdir()
    try:
        binary = app / 'eboot.bin'
        command = [str(tool), 'self', '--sign', '--in', str(native_elf),
                   '--out', str(binary), '--magic', '0x1D3D154F']
        with (output / 'self-build.log').open('wb') as log:
            subprocess.run(command, check=True, stdin=subprocess.DEVNULL,
                           stdout=log, stderr=subprocess.STDOUT)
        for name, expected in companions.items():
            source = relative_file(companion_root, name)
            target = relative_file(app, name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            if digest(target) != expected:
                raise ValueError('Companion changed while copying: ' + name)
        inspected = subprocess.check_output([str(tool), 'self', '--inspect', '--file', str(binary)],
                                            stderr=subprocess.STDOUT, text=True)
        (output / 'self-inspect.txt').write_text(inspected)
        if 'integrity: valid' not in inspected:
            raise ValueError('SELF integrity verification failed')
        if {p.relative_to(app).as_posix() for p in app.rglob('*') if p.is_file()} != required | {'eboot.bin'}:
            raise ValueError('Unexpected package contents')
        result = dict(status='PASS', eboot_sha256=digest(binary),
                      native_elf_sha256=digest(native_elf), verification_sha256=digest(verification),
                      focused_behavior_receipt_sha256=digest(behavior_receipt),
                      previous_full_application_receipt_sha256=source_proof['previous_full_application_receipt_sha256'],
                      previous_full_application_validation_reused=True,
                      fresh_full_application_execution=False,
                      filter_behavior_receipt_sha256=digest(native_elf.parent.parent.parent / 'filter-behavior/result.json'),
                      sort_filter=True, original_y_artwork_preserved=True,
                      xbox_splash=True, splash_input_sha256=splash_producer.INPUT_SHA256,
                      splash_behavior_receipt_sha256=digest(native_elf.parent.parent.parent / 'splash-behavior/result.json'),
                      splash_generated_locally=True,
                      companions=companions, tryon_delay_ms=125, installed=False, console_contact=False)
        (output / 'package-result.json').write_text(json.dumps(result, indent=2) + '\n')
        with zipfile.ZipFile(output / 'Editor-125ms-Sort-Filter-Splash.zip', 'x', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(app.rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(output).as_posix())
        return result
    except BaseException as error:
        (output / 'package-failed.json').write_text(json.dumps(dict(status='FAIL', error=str(error))) + '\n')
        raise
