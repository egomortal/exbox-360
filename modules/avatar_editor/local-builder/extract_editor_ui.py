"""Recover required UI PNGs locally from the user's supported editor XEX.

Uses a user-provided extraction tool or builds its pinned supplied source. No
tool source, executable, keys, artwork or title data are bundled with this app.
SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parent


class ExtractionError(ValueError):
    pass


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def byte_hash(data):
    return hashlib.sha256(data).hexdigest()


def overlap(a, b):
    return a == b or a.is_relative_to(b) or b.is_relative_to(a)


def xuiz_members(data):
    """Read bounded members; offsets start after the entire name table."""
    if len(data) < 22 or data[:4] != b'XUIZ':
        raise ExtractionError('Invalid resource archive')
    version = struct.unpack_from('>I', data, 4)[0]
    count = struct.unpack_from('>H', data, 20)[0]
    cursor, entries = 22, []
    for _ in range(count):
        if cursor + 9 > len(data):
            raise ExtractionError('Truncated resource table')
        size, offset, units = struct.unpack_from('>IIB', data, cursor)
        cursor += 9
        length = units * (2 if version == 1 else 1)
        if cursor + length > len(data):
            raise ExtractionError('Truncated resource name')
        name = data[cursor:cursor + length].decode('utf-16be' if version == 1 else 'latin1')
        cursor += length
        entries.append((name, offset, size))
    if any(cursor + offset + size > len(data) for _, offset, size in entries):
        raise ExtractionError('Truncated resource payload')
    # Matches the original consumer's last-entry-wins name lookup.
    return {name: data[cursor + offset:cursor + offset + size] for name, offset, size in entries}


def ui_bytes(image, contract):
    expected_pngs = json.loads((ROOT / 'companion-inputs.json').read_text())['input_roles']['ui_pngs']['files']
    recovered = {}
    for archive in contract['resource_archives']:
        offset = archive['virtual_address'] - contract['image_base']
        end = offset + archive['size']
        if offset < 0 or end > len(image):
            raise ExtractionError('Loaded image does not cover ' + archive['name'])
        data = image[offset:end]
        if byte_hash(data) != archive['sha256']:
            raise ExtractionError('Resource archive hash differs: ' + archive['name'])
        members = xuiz_members(data)
        for name in archive['pngs']:
            if name not in members or byte_hash(members[name]) != expected_pngs[name]:
                raise ExtractionError('Required PNG differs: ' + name)
            recovered[name] = members[name]
    if set(recovered) != set(expected_pngs):
        raise ExtractionError('Required PNG set is incomplete')
    return recovered


def build_tool(source, work, compiler, contract):
    source = Path(source).resolve(strict=True)
    expected = contract['provided_tool_source_files']
    actual_names = {p.relative_to(source).as_posix() for p in (source / 'src').rglob('*')
                    if p.is_file() and p.suffix.lower() in ('.cpp', '.h')}
    if actual_names != set(expected) - {'LICENSE'}:
        raise ExtractionError('Supplied extraction-tool source file set differs')
    for relative, expected_hash in expected.items():
        path = source / relative
        if path.is_symlink() or not path.resolve(strict=True).is_relative_to(source) or digest(path) != expected_hash:
            raise ExtractionError('Supplied extraction-tool source differs: ' + relative)
    snapshot = work / 'tool/source'
    snapshot.mkdir(parents=True, exist_ok=False)
    for relative in expected:
        destination = snapshot / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / relative, destination)
    units = sorted((snapshot / 'src').glob('*.cpp'))
    units += sorted((snapshot / 'src/crypto').glob('*.cpp'))
    units += sorted((snapshot / 'src/compress').glob('*.cpp'))
    binary = work / 'tool/xextool-re'
    command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', *map(str, units), '-o', str(binary)]
    with (work / 'tool/compile.log').open('wb') as log:
        subprocess.run(command, cwd=snapshot, stdin=subprocess.DEVNULL,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    if any(digest(source / name) != expected_hash for name, expected_hash in expected.items()):
        raise ExtractionError('Original extraction-tool source changed during compilation')
    return binary, command


def extract_from_xex(xex, work, *, tool=None, tool_source=None, compiler='c++'):
    contract_path = ROOT / 'xex-extract-contract.json'
    contract = json.loads(contract_path.read_text(encoding='utf-8'))
    if contract.get('schema') != 1:
        raise ExtractionError('Unsupported UI extraction contract')
    if (tool is None) == (tool_source is None):
        raise ExtractionError('Supply either the extraction tool or its source folder')
    xex, work = Path(xex).resolve(strict=True), Path(work).resolve()
    dependency = Path(tool if tool is not None else tool_source).resolve(strict=True)
    if any(overlap(work, path) for path in (xex, dependency, ROOT)):
        raise ExtractionError('Extraction workspace must be separate from inputs, tool and app')
    if work.exists():
        raise ExtractionError('Extraction workspace must be fresh')
    original_hash = digest(xex)
    if original_hash != contract['xex_sha256']:
        raise ExtractionError('Unsupported original editor XEX')
    work.mkdir(parents=True, exist_ok=False)
    receipt = dict(status='RUNNING', scope='local UI extraction from user original XEX',
                   xex_sha256=original_hash, contract_sha256=digest(contract_path),
                   native_editor_compiled=False, console_contact=False)

    def save():
        (work / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')

    try:
        save()
        if tool_source is not None:
            binary, command = build_tool(dependency, work, compiler, contract)
            receipt['tool_build_command'] = command
        else:
            binary = dependency
        tool_hash = digest(binary)
        with (work / 'tool-selftest.log').open('wb') as log:
            subprocess.run([str(binary), 'selftest'], cwd=work, stdin=subprocess.DEVNULL,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        image = work / 'loaded-image.bin'
        command = [str(binary), 'extract', str(xex), str(image)]
        with (work / 'extract.log').open('wb') as log:
            subprocess.run(command, cwd=work, stdin=subprocess.DEVNULL,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        payloads = ui_bytes(image.read_bytes(), contract)
        if digest(xex) != original_hash or digest(binary) != tool_hash:
            raise ExtractionError('Original XEX or extraction tool changed')
        destination = work / 'ui'
        destination.mkdir()
        outputs = {}
        for name, data in payloads.items():
            if Path(name).name != name or '/' in name or '\\' in name:
                raise ExtractionError('Unexpected output filename')
            (destination / name).write_bytes(data)
            outputs[name] = digest(destination / name)
        receipt.update(status='PASS', tool_sha256=tool_hash, command=command,
                       loaded_image_sha256=digest(image), pngs=outputs,
                       ui_directory=str(destination), original_unchanged=True,
                       resource_hashes={a['name']:a['sha256'] for a in contract['resource_archives']})
        save()
        return receipt
    except BaseException as error:
        receipt.update(status='FAIL', error=repr(error), original_unchanged=digest(xex) == original_hash)
        save()
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xex', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--tool', type=Path)
    source.add_argument('--tool-source', type=Path)
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    print(json.dumps(extract_from_xex(args.xex, args.work, tool=args.tool,
                                    tool_source=args.tool_source, compiler=args.compiler), indent=2))
