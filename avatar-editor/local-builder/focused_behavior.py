"""Run the original focused try-on harness against locally staged SDK headers.

This is a host fixture with mocked guest delegates, not a full editor run.
SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

from source_patch import relative_file


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(project, compiler, output, source=None, sdk=None):
    app = Path(__file__).resolve().parent
    project = Path(project).resolve(strict=True)
    source = Path(source).resolve(strict=True) if source else app / 'tryon'
    output = Path(output).resolve()
    # Keep compiler basename intact: clang++ may be a symlink to clang.
    compiler = Path(compiler).absolute()
    if not compiler.is_file():
        raise ValueError('Select an existing host Clang C++ compiler with sanitizer runtimes')
    for root in (project, source, app):
        if output == root or output.is_relative_to(root) or root.is_relative_to(output):
            raise ValueError('Focused gate output must be separate from supplied sources')
    if output.exists():
        raise ValueError('Focused gate requires a fresh output directory')
    proof_path = app / 'behavior-source-proof.json'
    proof = json.loads(proof_path.read_text())
    pins = {relative_file(source, name): value for name, value in proof['source_sha256'].items()}
    harness = relative_file(app, 'tests/tryon_adapter_test.cpp')
    pins[harness] = proof['focused_harness_sha256']
    for path, expected in pins.items():
        if digest(path) != expected:
            raise ValueError('Try-on source or harness differs: ' + path.name)
    include_names = [
        'private/av2/aer-work/generated/ae',
        'modules/avatar_editor/platform',
        'private/av2/toolchain/fast_float/include',
        'private/av2/sdk-host-source/include',
        'private/av2/sdk-host-source/thirdparty/simde',
        'private/av2/sdk-host-source/thirdparty/simde/simde',
        'private/av2/sdk-host-source/thirdparty/fmt/include',
        'private/av2/sdk-host-source/thirdparty/spdlog/include',
    ]
    includes = [relative_file(project, name) for name in include_names]
    if any(not path.is_dir() for path in includes):
        raise ValueError('Stage generated guest and host SDK headers before the focused gate')
    sdk_pins = {}
    if sdk is not None:
        sdk = Path(sdk).resolve(strict=True)
        if output == sdk or output.is_relative_to(sdk) or sdk.is_relative_to(output):
            raise ValueError('Focused gate output must be separate from supplied SDK')
        contract = json.loads((app / 'focused-sdk-inputs.json').read_text())
        for name, expected in contract['files'].items():
            path = relative_file(sdk, name)
            value = digest(path)
            if value not in expected:
                raise ValueError('Focused host SDK header version differs: ' + name)
            pins[path] = value
            sdk_pins[name] = value
        includes.append(relative_file(sdk, 'include'))
    if not any((path / 'rex/ppc.h').is_file() for path in includes):
        raise ValueError('Select the pinned user ReXGlue SDK using --sdk for host PPC headers')
    command = [str(compiler), '-std=c++23', '-O2', '-msse4.1',
               '-fPIE', '-mcmodel=large', '-fno-strict-aliasing', '-ffp-model=strict',
               '-fno-char8_t', '-Wall', '-Wextra', '-Werror',
               '-DREXGLUE_BUILD_CONFIG="Release"', '-DREX_PLATFORM_LINUX=1',
               '-DSPDLOG_COMPILED_LIB', '-DSPDLOG_FMT_EXTERNAL', '-DAV2_TRYON_TEST',
               '-I' + str(source)]
    for path in includes:
        command.extend(['-isystem', str(path)])
    command.extend([str(source / 'adapter.cpp'), str(harness),
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', '-pthread',
                    '-o', str(output / 'focused-tryon-test')])
    output.mkdir(parents=True, exist_ok=False)
    base = dict(scope=proof['focused_scope'], full_application_execution=False,
                native_execution=False, production_test_define=False,
                source_sha256=proof['source_sha256'], harness_sha256=pins[harness],
                source_proof_sha256=digest(proof_path),
                supplied_sdk_header_sha256=sdk_pins,
                previous_full_application_receipt_sha256=proof['previous_full_application_receipt_sha256'],
                previous_full_application_receipt_reused=True, command=command)
    try:
        with (output / 'compile.log').open('xb') as log:
            subprocess.run(command, stdin=subprocess.DEVNULL, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        with (output / 'run.log').open('xb') as log:
            subprocess.run([str(output / 'focused-tryon-test')], stdin=subprocess.DEVNULL,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        for path, expected in pins.items():
            if digest(path) != expected:
                raise ValueError('Try-on source changed during focused gate: ' + path.name)
        result = dict(base, status='PASS', focused_execution=True,
                      test_binary_sha256=digest(output / 'focused-tryon-test'))
        with (output / 'focused-behavior-result.json').open('x') as stream:
            json.dump(result, stream, indent=2)
            stream.write('\n')
        return result
    except BaseException as error:
        with (output / 'focused-behavior-failed.json').open('x') as stream:
            json.dump(dict(base, status='FAIL', error=str(error)), stream, indent=2)
            stream.write('\n')
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('project', 'compiler', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--sdk', type=Path)
    args = parser.parse_args()
    print(json.dumps(run(args.project, args.compiler, args.output, args.source, args.sdk), indent=2))
