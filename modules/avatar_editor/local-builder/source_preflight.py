"""Merge reviewed source manifests and report exact destination coverage.

This checks preparation metadata; it never claims a compiler or package passed.
SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import argparse
import hashlib
import json

from source_patch import PatchError, relative_file


def digest(data):
    return hashlib.sha256(data).hexdigest()


def relative_name(value):
    # Metadata-only destination check. Actual staging separately checks the
    # filesystem for symlinks; scanning absent output parents here adds no proof.
    if (not isinstance(value, str) or not value or '\\' in value or
            any(part in ('', '.', '..') or ':' in part for part in value.split('/'))):
        raise PatchError('Source destination must be a relative portable path')


def merge_manifests(recipe_root, manifests, overrides=None):
    """Return one stage_sources manifest; reject unapproved hash conflicts.

overrides is an explicitly reviewed {destination: authoritative_hash} mapping.
Equal output hashes are deduplicated without changing the source recipe.
"""
    root = Path(recipe_root).resolve(strict=True)
    overrides = overrides or {}
    selected, collisions, superseded, manifest_pins = {}, [], [], {}
    for relative in manifests:
        path = relative_file(root, relative)
        raw = path.read_bytes()
        manifest_pins[relative] = digest(raw)
        manifest = json.loads(raw)
        if manifest.get('schema') != 1:
            raise PatchError('Unsupported source manifest: ' + relative)
        entries = []
        for item in manifest.get('copies', []):
            entries.append((item['output'], item['output_sha256'], 'copies', item))
        for entry in manifest.get('transforms', []):
            variants, outputs = [], set()
            for variant in entry['variants']:
                recipe_path = relative_file(path.parent, variant['path'])
                recipe_raw = recipe_path.read_bytes()
                if digest(recipe_raw) != variant['sha256']:
                    raise PatchError('Transform recipe hash differs: ' + variant['path'])
                recipe = json.loads(recipe_raw)
                outputs.add((recipe['output'], recipe['output_sha256']))
                variants.append(dict(path=recipe_path.relative_to(root).as_posix(), sha256=variant['sha256']))
            if len(outputs) != 1:
                raise PatchError('Transform variants disagree: ' + entry['label'])
            destination, expected = next(iter(outputs))
            entries.append((destination, expected, 'transforms', dict(label=entry['label'], variants=variants)))
        for destination, expected, kind, item in entries:
            relative_name(destination)
            record = dict(destination=destination, sha256=expected, kind=kind,
                          manifest=relative, entry=item)
            previous = selected.get(destination)
            if previous is None:
                selected[destination] = record
            elif previous['sha256'] == expected:
                collisions.append(dict(destination=destination, sha256=expected,
                                       manifests=[previous['manifest'], relative], resolution='same-hash deduplicated'))
            else:
                authority = overrides.get(destination)
                if authority not in (previous['sha256'], expected):
                    raise PatchError('Conflicting source destination without exact reviewed override: ' + destination)
                winner, loser = (record, previous) if expected == authority else (previous, record)
                selected[destination] = winner
                superseded.append(dict(destination=destination, kept_sha256=authority,
                                       dropped_sha256=loser['sha256'], kept_manifest=winner['manifest'],
                                       dropped_manifest=loser['manifest']))
    for destination, expected in overrides.items():
        if destination not in selected or selected[destination]['sha256'] != expected:
            raise PatchError('Reviewed override has no matching provider: ' + destination)
    merged = dict(schema=1, copies=[], transforms=[])
    for record in selected.values():
        merged[record['kind']].append(record['entry'])
    return merged, dict(providers=list(selected.values()), collisions=collisions,
                        superseded=superseded, manifest_sha256=manifest_pins)


def coverage(required, providers, producers=(), overrides=None):
    overrides = overrides or {}
    available = {}
    for item in [*providers, *producers]:
        destination = item['destination']
        previous = available.get(destination)
        if previous and previous['sha256'] != item['sha256']:
            raise PatchError('Producer/source output conflict: ' + destination)
        available[destination] = item
    covered, missing, mismatches, replacements = [], [], [], []
    for item in required:
        destination, expected = item['destination'], item['sha256']
        if destination in overrides and overrides[destination] != expected:
            replacements.append(dict(destination=destination, inventory_sha256=expected,
                                     authoritative_sha256=overrides[destination]))
            expected = overrides[destination]
        found = available.get(destination)
        if found is None:
            missing.append(item)
        elif found['sha256'] != expected:
            mismatches.append(dict(destination=destination, required_sha256=expected,
                                   provider_sha256=found['sha256'], provider=found.get('manifest', found.get('producer'))))
        else:
            covered.append(dict(destination=destination, sha256=expected,
                                provider=found.get('manifest', found.get('producer'))))
    return dict(status='COVERAGE_COMPLETE_NOT_BUILT' if not missing and not mismatches else 'INCOMPLETE',
                required=len(required), covered_count=len(covered), covered=covered,
                missing=missing, hash_mismatches=mismatches, reviewed_inventory_replacements=replacements,
                native_compilation=False, package_ready=False)


def verify_staged(required, root, overrides=None):
    """Read-only final file verification, separate from metadata coverage."""
    overrides = overrides or {}
    root = Path(root).resolve(strict=True)
    failures = []
    for item in required:
        destination = item['destination']
        path = relative_file(root, destination)
        expected = overrides.get(destination, item['sha256'])
        if not path.is_file():
            failures.append(dict(destination=destination, reason='missing'))
        elif digest(path.read_bytes()) != expected:
            failures.append(dict(destination=destination, reason='hash differs'))
    return dict(status='PASS' if not failures else 'FAIL', scope='staged source hashes only',
                files=len(required), failures=failures, native_compilation=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--recipe-root', required=True, type=Path)
    parser.add_argument('--spec', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    spec = json.loads(args.spec.read_text(encoding='utf-8'))
    merged, details = merge_manifests(args.recipe_root, spec['manifests'],
                                      spec.get('manifest_overrides', spec.get('overrides')))
    result = coverage(spec['required'], details['providers'], spec.get('producers', []), spec.get('overrides'))
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(dict(merged_manifest=merged, merge=details, coverage=result), stream, indent=2)
        stream.write('\n')
    print(json.dumps({key:value for key,value in result.items()
                      if key not in ('covered', 'missing', 'hash_mismatches')}, indent=2))
