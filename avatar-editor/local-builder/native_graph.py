"""Execute a reviewed source build graph in a fresh local workspace.

SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_EXCEPTION
import argparse
import hashlib
import json
import re
import shutil
import subprocess

from source_patch import relative_file


class GraphError(ValueError):
    pass


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_archive(destination, members):
    """GNU ar with explicit member order, including duplicate names; index later."""
    names = [name for name, _ in members]
    if any(not name or '/' in name or '\\' in name or '\n' in name or
           not name.isascii() for name in names):
        raise GraphError('Archive member names must be ASCII basenames')
    table = bytearray()
    offsets = {}
    for name in names:
        if len(name) > 15 and name not in offsets:
            offsets[name] = len(table)
            table.extend(name.encode('ascii') + b'/\n')

    def header(name, size, mode='100644'):
        fields = [(name, 16), ('0', 12), ('0', 6), ('0', 6), (mode, 8), (str(size), 10)]
        if any(len(value) > width for value, width in fields):
            raise GraphError('Archive field exceeds format width')
        return ''.join(value.ljust(width) for value, width in fields).encode('ascii') + b'`\n'

    with Path(destination).open('xb') as target:
        target.write(b'!<arch>\n')
        if table:
            target.write(header('//', len(table), '0'))
            target.write(table)
            if len(table) % 2:
                target.write(b'\n')
        for name, path in members:
            size = Path(path).stat().st_size
            encoded = '/' + str(offsets[name]) if name in offsets else name + '/'
            target.write(header(encoded, size))
            with Path(path).open('rb') as source:
                shutil.copyfileobj(source, target)
            if size % 2:
                target.write(b'\n')


def execute(plan, run, dependencies, app):
    if plan.get('schema') != 1 or not plan.get('units'):
        raise GraphError('Missing source build graph')
    run = Path(run).resolve(strict=True)
    roots = dict(run=run, dependencies=Path(dependencies).resolve(strict=True),
                 app=Path(app).resolve(strict=True))
    if any(run == root or run.is_relative_to(root) or root.is_relative_to(run)
           for name, root in roots.items() if name != 'run'):
        raise GraphError('Build workspace overlaps app or dependencies')

    def expand(argv, **extra):
        if not isinstance(argv, list) or not argv or not all(isinstance(a, str) for a in argv):
            raise GraphError('Commands must be explicit argument arrays')
        output = []
        for arg in argv:
            for key, value in dict(roots, **extra).items():
                arg = arg.replace('${' + key + '}', str(value))
            if '${' in arg:
                raise GraphError('Unknown build path placeholder')
            output.append(arg)
        return output

    outputs, selected = {}, []
    for unit in plan['units']:
        if unit['id'] in outputs:
            raise GraphError('Duplicate compilation unit')
        source = relative_file(run, unit['source'])
        target = relative_file(run, unit['output'])
        if target in outputs.values() or target.exists():
            raise GraphError('Compilation outputs must be fresh and unique')
        if digest(source) != unit['source_sha256']:
            raise GraphError('Source version differs: ' + unit['id'])
        cwd = run if not unit.get('cwd') else relative_file(run, unit['cwd'])
        if not cwd.is_dir():
            raise GraphError('Compilation working directory does not exist: ' + unit['id'])
        argv = expand(unit['argv'])
        def command_path(option):
            if argv.count(option) != 1 or argv.index(option) + 1 >= len(argv):
                return None
            path = Path(argv[argv.index(option) + 1])
            return (path if path.is_absolute() else cwd / path).resolve()
        if command_path('-o') != target:
            raise GraphError('Compiler output differs from declared output')
        if command_path('-c') != source:
            raise GraphError('Compiler input differs from declared source')
        flags = [arg for arg in argv if re.fullmatch(r'-O(?:[0-3szg]|fast)', arg)]
        if unit.get('optimization') and (not flags or flags[-1] != unit['optimization']):
            raise GraphError('Effective optimization differs: ' + unit['id'])
        outputs[unit['id']] = target
        selected.append((unit, argv, target, cwd))
    archive_paths = set()
    for archive in plan.get('archives', []):
        path = relative_file(run, archive['path'])
        if path.exists() or path in archive_paths or path in outputs.values():
            raise GraphError('Archive outputs must be fresh and unique')
        archive_paths.add(path)
        if not archive['members'] or any(m['unit'] not in outputs for m in archive['members']):
            raise GraphError('Archive requires a source-built member')

    logroot = run / 'native-logs'
    logroot.mkdir(exist_ok=False)

    def compile_unit(index, entry):
        unit, argv, target, cwd = entry
        print(f"Compile {index}/{len(selected)}: {unit['id']}", flush=True)
        target.parent.mkdir(parents=True, exist_ok=True)
        with (logroot / f'{index:04d}.log').open('wb') as log:
            subprocess.run(argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
            for command in unit.get('postprocess', []):
                subprocess.run(expand(command), cwd=cwd, stdin=subprocess.DEVNULL,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        if not target.is_file() or not target.stat().st_size:
            raise GraphError('Compiler did not produce an object: ' + unit['id'])
        return dict(id=unit['id'], sha256=digest(target))

    records = []
    # Submit only two units at a time. A failed batch never starts later units;
    # running children finish and retain their objects and individual logs.
    # Collect in graph order so archive membership and receipts stay stable.
    with ThreadPoolExecutor(max_workers=2, thread_name_prefix='editor-compile') as pool:
        for offset in range(0, len(selected), 2):
            pending = [pool.submit(compile_unit, index + 1, selected[index])
                       for index in range(offset, min(offset + 2, len(selected)))]
            try:
                done, _ = wait(pending, return_when=FIRST_EXCEPTION)
                for future in pending:
                    if future in done and future.exception() is not None:
                        future.result()
                records.extend(future.result() for future in pending)
            except BaseException:
                for future in pending:
                    future.cancel()
                raise
    archives = []
    for archive in plan.get('archives', []):
        target = relative_file(run, archive['path'])
        target.parent.mkdir(parents=True, exist_ok=True)
        write_archive(target, [(m['name'], outputs[m['unit']]) for m in archive['members']])
        subprocess.run(expand(archive['index_argv'], archive=target), cwd=run,
                       stdin=subprocess.DEVNULL, check=True)
        archives.append(dict(path=archive['path'], sha256=digest(target)))
    receipt = dict(status='PASS', scope='source compilation and archives only',
                   units=records, archives=archives)
    with (run / 'native-graph-result.json').open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2)
        stream.write('\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('plan', 'run', 'dependencies', 'app'):
        parser.add_argument('--' + option, required=True, type=Path)
    args = parser.parse_args()
    execute(json.loads(args.plan.read_text(encoding='utf-8')), args.run, args.dependencies, args.app)
