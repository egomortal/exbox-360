"""Local build entry point; no network, upload or console operations."""
from pathlib import Path
import argparse
import hashlib
import json
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent


class BuildError(Exception):
    pass


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def overlap(a, b):
    return a == b or a.is_relative_to(b) or b.is_relative_to(a)


def validate_paths(inputs, dependencies, work):
    values = {name: Path(value).expanduser().resolve() for name, value in
              [('inputs', inputs), ('dependencies', dependencies), ('work', work)]}
    for name in ['inputs', 'dependencies']:
        if not values[name].is_dir():
            raise BuildError('Select an existing ' + name + ' folder.')
    if any(overlap(path, ROOT) for path in values.values()):
        raise BuildError('Keep inputs, dependencies and output outside this app folder.')
    if any(overlap(values['work'], values[name]) for name in ['inputs', 'dependencies']):
        raise BuildError('Output must not overlap originals or dependencies.')
    return values


def build(inputs, dependencies, work):
    values = validate_paths(inputs, dependencies, work)
    recipe_path = ROOT / 'pipeline.json'
    recipe = json.loads(recipe_path.read_text(encoding='utf-8'))
    if recipe.get('schema') != 1:
        raise BuildError('Unsupported build recipe.')
    if recipe.get('release_ready') is not True:
        raise BuildError('Development preview: the source-to-package recipe is not complete. '
                         'No input files were changed. See BUILD-STATUS.md.')
    kinds = {stage['kind'] for stage in recipe['stages']}
    if not {'prepare', 'generate', 'compile', 'link', 'verify', 'package'}.issubset(kinds):
        raise BuildError('Incomplete native build recipe.')
    for item in recipe['required_inputs']:
        base = values[item['root']]
        path = (base / item['path']).resolve()
        if not path.is_relative_to(base) or not path.is_file():
            raise BuildError('Missing required input: ' + item['label'])
        if digest(path) != item['sha256']:
            raise BuildError('Unsupported input version: ' + item['label'])
    run = values['work'] / time.strftime('build-%Y%m%d-%H%M%S')
    run.mkdir(parents=True, exist_ok=False)
    values.update(run=run, app=ROOT, python=sys.executable)
    receipt = {'status': 'RUNNING', 'recipe_sha256': digest(recipe_path), 'stages': []}

    def save():
        pending = run / 'result.pending'
        pending.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        pending.replace(run / 'result.json')

    def expand(arg):
        for key, value in values.items():
            arg = arg.replace('${' + key + '}', str(value))
        if '${' in arg:
            raise BuildError('Unknown recipe path.')
        return arg

    try:
        save()
        for index, stage in enumerate(recipe['stages'], 1):
            print(f"[{index}/{len(recipe['stages'])}] {stage['label']}", flush=True)
            with (run / f'{index:02d}.log').open('w', encoding='utf-8') as log:
                process = subprocess.Popen([expand(arg) for arg in stage['argv']], cwd=run,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           stdin=subprocess.DEVNULL, shell=False, text=True,
                                           encoding='utf-8', errors='replace', bufsize=1)
                try:
                    for line in process.stdout:
                        log.write(line)
                        log.flush()
                        print(line, end='', flush=True)
                    code = process.wait()
                except BaseException:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                    raise
                finally:
                    process.stdout.close()
            receipt['stages'].append({'label': stage['label'], 'exit_code': code})
            save()
            if code:
                raise BuildError(stage['label'] + ' failed; local build logs were preserved.')
        package = (run / recipe['package']).resolve()
        if not package.is_relative_to(run) or not package.is_file() or not package.stat().st_size:
            raise BuildError('No package was produced inside the build folder.')
        receipt.update(status='PASS', package=str(package), sha256=digest(package))
        save()
        print('Build complete: ' + str(package), flush=True)
    except BaseException as error:
        receipt.update(status='CANCELLED' if isinstance(error, KeyboardInterrupt) else 'FAIL')
        save()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['inputs', 'dependencies', 'work']:
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    def cancel(*_):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, cancel)
    if hasattr(signal, 'SIGBREAK'):
        signal.signal(signal.SIGBREAK, cancel)
    try:
        build(args.inputs, args.dependencies, args.work)
    except KeyboardInterrupt:
        print('Cancelled; existing files were preserved.', flush=True)
        return 130
    except (BuildError, OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr, flush=True)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
