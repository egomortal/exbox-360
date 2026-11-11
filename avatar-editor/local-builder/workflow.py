"""Local source preparation and native compilation from supplied inputs.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def receipt(run, name, value):
    path = run / 'workflow-receipts' / (name + '.json')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as out:
        json.dump(value, out, indent=2)
    return value


def separated(run, inputs):
    paths = list(inputs['sources'].values()) + list(inputs['originals'].values())
    paths += [inputs['dependencies'], str(ROOT)]
    for value in paths:
        other = Path(value).resolve(strict=True)
        if run == other or run.is_relative_to(other) or other.is_relative_to(run):
            raise ValueError('Build workspace must be separate from supplied inputs')


def prepare(inputs, run, spec):
    from source_preflight import merge_manifests
    from stage_sources import stage
    if run.exists():
        raise ValueError('Preparation requires a fresh workspace; preserve failed attempts')
    separated(run, inputs)
    roots = {k: Path(v).resolve(strict=True) for k, v in inputs['sources'].items()}
    roots.update(app=ROOT, builder_source=ROOT)
    manifest, details = merge_manifests(ROOT, spec['manifests'], spec.get('manifest_overrides'))
    stage(manifest, ROOT, roots, run)
    return receipt(run, 'prepare', {'status': 'PASS', 'manifest_sha256': details['manifest_sha256'],
                                   'native_compilation': False})


def fresh_verified(run, relative, data, expected):
    from source_patch import relative_file
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('Generated source hash differs: ' + relative)
    path = relative_file(run, relative)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as out:
        out.write(data)


def generate(inputs, run, spec):
    import guest_codegen
    import extract_editor_ui
    import caption_header
    import generate_config_headers
    import builtin_shader_headers
    import mesa_producer
    import prepare_pch
    from source_preflight import verify_staged
    separated(run, inputs)
    if read(run / 'workflow-receipts/prepare.json')['status'] != 'PASS':
        raise ValueError('Source staging has not passed')
    attempt = run / 'workflow-receipts/generate-started.json'
    with attempt.open('x') as out:
        json.dump({'status': 'STARTED'}, out)
    tools = inputs['tools']
    avatar = Path(inputs['sources']['avatar_editor'])
    results = {}
    try:
        print('Generating guest source from supplied XEX', flush=True)
        results['guest'] = guest_codegen.generate(avatar, Path(inputs['originals']['xex']),
                                                  Path(tools['rexglue']), run)
        ui = run / 'ui-extraction'
        print('Extracting supplied UI resources and generating captions', flush=True)
        results['ui'] = extract_editor_ui.extract_from_xex(Path(inputs['originals']['xex']), ui,
            tool_source=Path(tools['xex_source']), compiler=tools.get('host_cxx', 'c++'))
        results['captions'] = caption_header.generate(ui / 'loaded-image.bin', run)
        config = read(ROOT / 'config-header-contract.json')
        print('Generating source-derived configuration headers', flush=True)
        xr = avatar / 'tools/XenosRecomp'
        for name, hashes in config['xenos']['inputs'].items():
            if hashlib.sha256((xr / name).read_bytes()).hexdigest() not in hashes:
                raise ValueError('Xenos generator input differs: ' + name)
        data, fingerprint = generate_config_headers.xr_header(xr / 'XenosRecomp')
        if fingerprint != config['xenos']['translator_fingerprint']:
            raise ValueError('Xenos translator fingerprint differs')
        for name in config['xenos']['outputs']:
            fresh_verified(run, name, data, config['xenos']['output_sha256'])
        headers = generate_config_headers.ffmpeg_headers(run / config['ffmpeg']['source'])
        for name, item in config['ffmpeg']['outputs'].items():
            fresh_verified(run, item['path'], headers[name], item['sha256'])
        print('Generating builtin shaders from supplied source', flush=True)
        results['shaders'] = builtin_shader_headers.build(avatar / 'src/gpu/shaders',
            Path(tools['shader_tools']), run / 'source/project/private/av2/q6b-shaders',
            read(ROOT / 'builtin-shader-contract.json'))
        print('Generating Mesa source and headers', flush=True)
        results['mesa'] = mesa_producer.produce(read(ROOT / 'mesa-producers.json'), run,
                                               Path(inputs['dependencies']))
        print('Preparing portable PCH wrappers and verifying all required source hashes', flush=True)
        results['pch_wrappers'] = prepare_pch.prepare(run)
        verified = verify_staged(spec['required'], run, spec.get('overrides'))
        receipt(run, 'source-verification', verified)
        if verified['status'] != 'PASS':
            raise ValueError('Final source verification failed; see source-verification.json')
        return receipt(run, 'generate', {'status': 'PASS', 'files': verified['files'],
            'native_compilation': False, 'producers': results})
    except Exception as error:
        receipt(run, 'generate-failed', {'status': 'FAIL', 'error': str(error)})
        raise


def compile_native(inputs, run, spec, graph_path):
    from source_preflight import verify_staged
    from source_patch import relative_file
    import native_graph
    from dependency_aliases import for_run
    separated(run, inputs)
    inputs = for_run(inputs, run)
    if read(run / 'workflow-receipts/generate.json')['status'] != 'PASS':
        raise ValueError('Source generation has not passed')
    if verify_staged(spec['required'], run, spec.get('overrides'))['status'] != 'PASS':
        raise ValueError('Prepared source files changed')
    dependencies = Path(inputs['dependencies']).resolve(strict=True)
    graph = read(graph_path)
    for unit in graph['units']:
        if unit.get('cwd'):
            relative_file(run, unit['cwd']).mkdir(parents=True, exist_ok=True)
    pch = read(ROOT / 'pch-producer.json')
    prepare_include_directories(run, graph, pch)
    output = relative_file(run, pch['native_output'])
    if output.exists():
        raise ValueError('Native PCH already exists; preserve the previous attempt')
    argv = [s.replace('${run}', str(run)).replace('${dependencies}', str(dependencies))
            for s in pch['native_compile_argv']]
    if any('${' in s for s in argv):
        raise ValueError('Unresolved PCH command placeholder')
    stable_run = Path(run).resolve(strict=True)
    argv = (native_graph.stable_defines(argv, stable_run, dependencies)
            + native_graph.stable_path_flags(stable_run, dependencies))
    attempt_root = run / 'workflow-receipts'
    index = 1
    while (attempt_root / ('compile-attempt-%02d' % index)).exists():
        index += 1
    attempt = attempt_root / ('compile-attempt-%02d' % index)
    attempt.mkdir()
    try:
        with (attempt / 'pch-compile.log').open('xb') as log:
            subprocess.run(argv, cwd=relative_file(run, pch['cwd']), stdin=subprocess.DEVNULL,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    except Exception as error:
        (attempt / 'result.json').write_text(json.dumps({'status': 'FAIL', 'stage': 'PCH',
            'error': str(error), 'native_graph_started': False}, indent=2))
        raise
    if not output.is_file() or not output.stat().st_size:
        raise ValueError('Native PCH compiler did not produce its output')
    receipt(run, 'pch-compile', {'status': 'PASS', 'sha256': native_graph.digest(output)})
    return native_graph.execute(graph, run, dependencies, ROOT)


def prepare_include_directories(run, graph, pch):
    """Preserve lexical ../ include traversal without changing compiler flags."""
    run = Path(run).resolve(strict=True)
    created = []
    for argv in [u['argv'] for u in graph['units']] + [pch['native_compile_argv']]:
        for index, argument in enumerate(argv):
            value = argument[2:] if argument.startswith('-I${run}/') else None
            if index and argv[index - 1] in ('-I', '-isystem', '-iquote'):
                value = argument
            if not value or not value.startswith('${run}/'):
                continue
            current = run
            for part in value.removeprefix('${run}/').split('/'):
                if part == '..':
                    current = current.parent
                elif part not in ('', '.'):
                    current = current / part
                if not current.resolve().is_relative_to(run) or current.is_symlink():
                    raise ValueError('Include directory escapes workspace')
                if not current.exists():
                    current.mkdir()
                    created.append(current.relative_to(run).as_posix())
                elif not current.is_dir():
                    raise ValueError('Include directory is not a folder')
    return {'created': created, 'compiler_flags_changed': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'generate', 'compile'])
    parser.add_argument('--inputs', required=True, type=Path)
    parser.add_argument('--run', required=True, type=Path)
    parser.add_argument('--spec', type=Path, default=ROOT / 'source-preflight.json')
    parser.add_argument('--graph', type=Path, default=ROOT / 'native-plan.json')
    args = parser.parse_args()
    inputs = read(args.inputs)
    if inputs.get('schema') != 1:
        raise ValueError('Unsupported local input map')
    run = args.run.resolve()
    spec = read(args.spec)
    action = {'prepare': prepare, 'generate': generate}
    result = (compile_native(inputs, run, spec, args.graph) if args.action == 'compile'
              else action[args.action](inputs, run, spec))
    print(json.dumps({'action': args.action, 'status': result.get('status'),
                      'receipt_directory': str(run / 'workflow-receipts')}, indent=2))


if __name__ == '__main__':
    main()
