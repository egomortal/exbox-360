"""Link freshly compiled editor objects with the pinned user SDK.

SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import json
import subprocess
from native_verify import digest
from source_patch import relative_file, sha256


def link(run, dependencies, app=None):
    run = Path(run).resolve(strict=True)
    dependencies = Path(dependencies).resolve(strict=True)
    app = Path(app or Path(__file__).parent).resolve(strict=True)
    plan = json.loads((app / 'link-plan.json').read_text())
    graph = json.loads((app / 'native-plan.json').read_text())
    built = json.loads((run / 'native-graph-result.json').read_text())
    if built.get('status') != 'PASS':
        raise ValueError('Source compilation has not passed')
    units = {unit['id']: unit for unit in graph['units']}
    records = {unit['id']: unit for unit in built['units']}
    if len(records) != len(built['units']) or set(records) != set(units):
        raise ValueError('Compilation receipt has a different unit set')
    for key, unit in units.items():
        if digest(relative_file(run, unit['output'])) != records[key]['sha256']:
            raise ValueError('Compiled object changed: ' + key)
    archives = {item['path']: item['sha256'] for item in built['archives']}
    if set(archives) != {item['path'] for item in graph['archives']}:
        raise ValueError('Archive receipt has a different archive set')
    for path, expected in archives.items():
        if digest(relative_file(run, path)) != expected:
            raise ValueError('Compiled archive changed: ' + path)

    def expand(value):
        for key, path in dict(run=run, dependencies=dependencies, app=app).items():
            value = value.replace('${' + key + '}', str(path))
        if '${' in value:
            raise ValueError('Unknown linker path placeholder')
        return value

    for required in plan['required_files']:
        if digest(expand(required['path'])) != required['sha256']:
            raise ValueError('Unsupported link dependency: ' + required['role'] + ' ' + required['path'])
    link_argv = [expand(arg) for arg in plan['link_argv']]
    convert_argv = [expand(arg) for arg in plan['convert_argv']]
    targets = [Path(link_argv[link_argv.index(flag) + 1]) for flag in ['-o', '-Map']]
    targets.append(Path(convert_argv[convert_argv.index('--out') + 1]))
    for job in plan['source_stub_jobs']:
        targets.extend([Path(expand(job['object'])), Path(expand(job['output']))])
    for target in targets:
        if target.exists() or not target.resolve().is_relative_to(run):
            raise ValueError('Link outputs must be fresh and inside the workspace')
    logs = run / 'native/link-logs'
    logs.mkdir(parents=True, exist_ok=False)
    results = []

    def invoke(argv, label):
        print('Link: ' + label, flush=True)
        with (logs / (label + '.log')).open('wb') as log:
            subprocess.run([expand(arg) for arg in argv], cwd=run, stdin=subprocess.DEVNULL,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        results.append(label)

    try:
        for target in targets:
            target.parent.mkdir(parents=True, exist_ok=True)
        mapping = plan['generated_link_map']
        data = mapping['content'].encode('utf-8')
        if sha256(data) != mapping['sha256']:
            raise ValueError('Generated linker map differs')
        map_path = relative_file(run, mapping['path'])
        map_path.parent.mkdir(parents=True, exist_ok=True)
        with map_path.open('xb') as stream:
            stream.write(data)
        for job in plan['source_stub_jobs']:
            for index, command in enumerate(job['commands']):
                invoke(command, job['name'] + '-' + str(index + 1))
        invoke(plan['link_argv'], 'editor')
        invoke(plan['convert_argv'], 'convert')
        result = dict(status='PASS', scope='fresh native linking only',
                      graph_receipt_sha256=digest(run / 'native-graph-result.json'),
                      link_plan_sha256=digest(app / 'link-plan.json'),
                      llvm_elf_sha256=digest(targets[0]), native_elf_sha256=digest(targets[2]),
                      completed=results)
        (logs / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        return result
    except BaseException as error:
        (logs / 'failure.json').write_text(json.dumps(dict(status='FAIL', error=str(error),
            completed=results), indent=2) + '\n')
        raise
