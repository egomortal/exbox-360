"""Apply reviewed transforms to user-selected sources; originals stay unchanged.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import json
from pathlib import Path
from source_patch import PatchError, apply, relative_file, sha256


def stage(manifest, recipes, roots, output):
    if manifest.get('schema') != 1 or not (manifest.get('transforms') or manifest.get('copies')):
        raise PatchError('No reviewed source transform manifest')
    recipes = Path(recipes).resolve(strict=True)
    output = Path(output).resolve()
    if output.exists():
        raise PatchError('Source staging needs a fresh output directory')
    if any(output == Path(root).resolve() or output.is_relative_to(Path(root).resolve())
           or Path(root).resolve().is_relative_to(output) for root in roots.values()):
        raise PatchError('Generated source must be separate from supplied originals')
    selected, outputs, hashes, copied = [], set(), {}, []
    for entry in manifest.get('copies', []):
        if entry['root'] not in roots:
            raise PatchError('Select dependency source: ' + entry['root'])
        source = relative_file(roots[entry['root']], entry['path'])
        raw = source.read_bytes()
        if sha256(raw) not in entry['input_sha256']:
            raise PatchError('Unsupported source version: ' + entry['path'])
        candidates = [raw, raw.replace(b'\r\n', b'\n'),
                      raw.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')]
        matches = [data for data in candidates if sha256(data) == entry['output_sha256']]
        if not matches:
            raise PatchError('Copy output differs: ' + entry['path'])
        if entry['output'] in outputs:
            raise PatchError('Duplicate generated source destination')
        outputs.add(entry['output'])
        copied.append((entry['output'], matches[0]))
    for entry in manifest.get('transforms', []):
        matches = []
        for variant in entry['variants']:
            raw = relative_file(recipes, variant['path']).read_bytes()
            if sha256(raw) != variant['sha256']:
                raise PatchError('Source recipe differs: ' + variant['path'])
            recipe = json.loads(raw)
            supported = True
            for item in recipe['inputs'].values():
                if item['root'] not in roots:
                    raise PatchError('Select dependency source: ' + item['root'])
                source = relative_file(roots[item['root']], item['path'])
                if not source.is_file():
                    supported = False
                    break
                if source not in hashes:
                    hashes[source] = sha256(source.read_bytes())
                if hashes[source] != item['sha256']:
                    supported = False
                    break
            if supported:
                matches.append(recipe)
        if not matches:
            raise PatchError('Missing or unsupported source version: ' + entry['label'])
        recipe = matches[0]
        if any((r['output'], r['output_sha256']) !=
               (recipe['output'], recipe['output_sha256']) for r in matches):
            raise PatchError('Ambiguous source variants: ' + entry['label'])
        if recipe['output'] in outputs:
            raise PatchError('Duplicate generated source destination')
        outputs.add(recipe['output'])
        selected.append(recipe)
    output.mkdir(parents=True, exist_ok=False)
    results = []
    try:
        for name, raw in copied:
            destination = relative_file(output, name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open('xb') as stream:
                stream.write(raw)
            results.append(dict(path=name, sha256=sha256(raw), bytes=len(raw)))
        for recipe in selected:
            results.append(apply(recipe, roots, output))
    except BaseException:
        (output / 'staging-failed.json').write_text(json.dumps({
            'status': 'FAIL', 'completed': results}, indent=2) + '\n', encoding='utf-8')
        raise
    receipt = dict(status='PASS', scope='source transforms only', files=results)
    (output / 'staging-result.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt
