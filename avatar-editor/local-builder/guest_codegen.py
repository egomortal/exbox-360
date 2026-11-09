"""Generate reviewed guest sources locally from user-supplied original files.

Requires an existing compatible ReXGlue 0.10.0 host tool. No downloads, native
compilation or console operations. Generated title bodies stay in the workspace.
SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tomllib


class CodegenError(ValueError):
    pass


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def overlap(a, b):
    return a == b or a.is_relative_to(b) or b.is_relative_to(a)


def patch_native_pch(data):
    """Existing reviewed native PS5 address-skew selection; no new policy."""
    text = data.decode('utf-8').replace('\r\n', '\n')
    old = '#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)'
    if text.count(old) != 1 or text.count('#define REX_RAW_ADDR(x)') != 1:
        raise CodegenError('Generated PCH does not match the reviewed native patch')
    text = text.replace(old, '// Native PS5 also uses 16 KiB allocation granularity and the E-heap skew.\n'
                        '#if REX_PLATFORM_WIN32 || REX_PLATFORM_PS5 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)')
    checks = ('#if REX_PLATFORM_PS5\n'
              'static_assert(REX_PHYS_HOST_OFFSET(0xFFCA2000u)==0x1000u, "PS5 E-heap CPU skew must match runtime");\n'
              'static_assert(REX_PHYS_HOST_OFFSET(0xDFFFFFFFu)==0u, "non-E CPU addresses remain unchanged");\n'
              '#endif\n\n')
    text = text.replace('#define REX_RAW_ADDR(x)', checks + '#define REX_RAW_ADDR(x)', 1)
    return text.encode('utf-8')


def generate(upstream, xex, tool, run, contract_path=None):
    contract_path = Path(contract_path or Path(__file__).with_name('guest-contract.json'))
    contract = json.loads(contract_path.read_text(encoding='utf-8'))
    if contract.get('schema') != 1:
        raise CodegenError('Unsupported guest generation contract')
    upstream, xex, tool = (Path(value).resolve(strict=True) for value in (upstream, xex, tool))
    run = Path(run).resolve()
    if not upstream.is_dir() or not xex.is_file() or not tool.is_file():
        raise CodegenError('Supply an upstream source folder, original XEX and ReXGlue executable')
    if any(overlap(run, path) for path in (upstream, xex, tool, Path(__file__).resolve().parent)):
        raise CodegenError('Workspace must be separate from original inputs, generator and app')
    job = run / 'guest-codegen'
    base = run / 'source/project/private/av2/aer-work/generated/ae'
    native = run / 'source/project/private/q7o-guest-skew-20261103/generated'
    for path in (job, base, native):
        if path.exists() or not path.resolve().is_relative_to(run):
            raise CodegenError('Guest generation requires fresh workspace destinations')
    inputs = {xex: digest(xex), tool: digest(tool)}
    if inputs[xex] != contract['xex_sha256']:
        raise CodegenError('Original XEX version is unsupported')
    copied = {}
    for relative, accepted in contract['upstream_inputs'].items():
        path = upstream / relative
        if path.is_symlink() or not path.resolve(strict=True).is_relative_to(upstream):
            raise CodegenError('Upstream input escapes its source folder')
        actual = digest(path)
        if actual not in accepted:
            raise CodegenError('Unsupported upstream input version: ' + relative)
        inputs[path] = actual
        copied[relative] = path.read_bytes().replace(b'\r\n', b'\n')
    manifest = tomllib.loads(copied['ae_manifest.toml'].decode('utf-8'))
    entry = manifest['entrypoint']
    if (manifest['project']['sdk_version'] != '0.10.0' or
            entry['file_path'] != 'assets/avatareditor.xex' or
            entry['out_directory_path'] != 'generated/ae' or
            entry['includes'] != ['config/ae_funcs.toml', 'config/ae_hooks.toml']):
        raise CodegenError('Manifest paths or SDK differ from the reviewed contract')
    job.mkdir(parents=True, exist_ok=False)
    receipt = dict(status='RUNNING', scope='local guest code generation only',
                   contract_sha256=digest(contract_path), tool_sha256=inputs[tool],
                   xex_sha256=inputs[xex], native_compilation=False, console_contact=False)

    def save():
        (job / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')

    try:
        save()
        version = subprocess.run([str(tool), '--version'], cwd=job, check=True,
                                 stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, encoding='utf-8').stdout.strip()
        (job / 'version.txt').write_text(version + '\n', encoding='utf-8')
        if not re.fullmatch(r'0\.10\.0(?:\.0)?(?:[-+][A-Za-z0-9_.-]+)?', version):
            raise CodegenError('Compatible ReXGlue 0.10.0 is required; got ' + version)
        receipt['tool_version'] = version
        for relative, data in copied.items():
            destination = job / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        original_copy = job / 'assets/avatareditor.xex'
        original_copy.parent.mkdir()
        shutil.copyfile(xex, original_copy)
        if digest(original_copy) != contract['xex_sha256']:
            raise CodegenError('Workspace XEX copy differs')
        command = [str(tool), 'codegen', str(job / 'ae_manifest.toml')]
        receipt['command'] = command
        save()
        print('Generating guest sources from your original XEX...', flush=True)
        with (job / 'codegen.log').open('wb') as log:
            subprocess.run(command, cwd=job, stdin=subprocess.DEVNULL,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        generated = job / 'generated/ae'
        actual = {path.name for path in generated.iterdir()
                  if path.suffix in ('.cpp', '.h', '.hpp', '.inc')}
        if actual != set(contract['generated_files']):
            raise CodegenError('Generated source/header file set differs from the contract')
        mismatches = []
        for name, expected in contract['generated_files'].items():
            if digest(generated / name) != expected:
                mismatches.append(name)
        for name, expected in contract.get('additional_outputs', {}).items():
            if digest(generated / name) != expected:
                mismatches.append(name)
        if mismatches:
            receipt['hash_mismatches'] = mismatches
            raise CodegenError('Generated hashes differ: ' + ', '.join(mismatches))
        patched = patch_native_pch((generated / 'ae_pch.h').read_bytes())
        if hashlib.sha256(patched).hexdigest() != contract['native_files']['ae_pch.h']:
            raise CodegenError('Reviewed native PCH patch output differs')
        for name, expected in contract['native_files'].items():
            actual_hash = hashlib.sha256(patched).hexdigest() if name == 'ae_pch.h' else digest(generated / name)
            if actual_hash != expected:
                raise CodegenError('Native generated source differs: ' + name)
        if any(digest(path) != expected for path, expected in inputs.items()):
            raise CodegenError('Original input or tool changed during generation')
        base.mkdir(parents=True, exist_ok=False)
        native.mkdir(parents=True, exist_ok=False)
        for name in contract['generated_files']:
            shutil.copyfile(generated / name, base / name)
            if name == 'ae_pch.h':
                (native / name).write_bytes(patched)
            else:
                shutil.copyfile(generated / name, native / name)
        for name in contract.get('additional_outputs', {}):
            shutil.copyfile(generated / name, base / name)
        for directory, expected_files in [(base, contract['generated_files']), (native, contract['native_files'])]:
            if any(digest(directory / name) != expected for name, expected in expected_files.items()):
                raise CodegenError('Final staged source hash differs')
        receipt.update(status='PASS', cpp_sources=sum(name.endswith('.cpp') for name in actual),
                       generated_files=len(actual), native_files=len(contract['native_files']),
                       originals_unchanged=True, base_output=str(base), native_output=str(native))
        save()
        print('Guest generation verified: 42 sources and all reviewed headers.', flush=True)
        return receipt
    except BaseException as error:
        receipt.update(status='FAIL', error=repr(error),
                       originals_unchanged=all(digest(path) == expected for path, expected in inputs.items()))
        save()
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('upstream', 'xex', 'tool', 'run'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    generate(args.upstream, args.xex, args.tool, args.run)
