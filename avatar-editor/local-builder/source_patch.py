"""Reconstruct ported source locally from hash-pinned, user-supplied files.

Recipes contain byte ranges and reviewed authored additions, not upstream bodies.
SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import hashlib
import json
import re


class PatchError(ValueError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def relative_file(root, relative):
    root = Path(root).resolve(strict=True)
    # The portable recipe always uses POSIX relative names, also on Windows.
    if not isinstance(relative, str) or not relative or '\\' in relative:
        raise PatchError('Invalid relative source path')
    parts = relative.split('/')
    if any(part in ('', '.', '..') or ':' in part for part in parts):
        raise PatchError('Source path must stay inside its selected folder')
    path = root.joinpath(*parts)
    if any(parent.is_symlink() for parent in [path, *path.parents] if parent != root):
        raise PatchError('Symlink source paths are unsupported')
    if not path.resolve().is_relative_to(root):
        raise PatchError('Source path escapes selected folder')
    return path


def reconstruct(recipe, roots):
    if recipe.get('schema') != 1:
        raise PatchError('Unsupported source transform schema')
    sources = {}
    for key, source in recipe['inputs'].items():
        if source['root'] not in roots:
            raise PatchError('Missing input role: ' + source['root'])
        path = relative_file(roots[source['root']], source['path'])
        data = path.read_bytes()
        if sha256(data) != source['sha256']:
            raise PatchError('Unsupported source version: ' + source['path'])
        sources[key] = data
    chunks = []
    for operation in recipe['operations']:
        if set(operation) == {'copy', 'offset', 'length'}:
            data = sources[operation['copy']]
            offset, length = operation['offset'], operation['length']
            if (type(offset) is not int or type(length) is not int or
                    offset < 0 or length < 0 or offset + length > len(data)):
                raise PatchError('Invalid copy range')
            chunks.append(data[offset:offset + length])
        elif set(operation) == {'authored_utf8'}:
            chunks.append(operation['authored_utf8'].encode('utf-8'))
        else:
            raise PatchError('Unknown source transform operation')
    result = b''.join(chunks)
    expected = recipe['output_sha256']
    if not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected):
        raise PatchError('Missing output source hash')
    if sha256(result) != expected:
        raise PatchError('Reconstructed source hash differs')
    return result


def apply(recipe, roots, output_root):
    output_root = Path(output_root).resolve(strict=True)
    for root in roots.values():
        source_root = Path(root).resolve(strict=True)
        if (output_root == source_root or output_root.is_relative_to(source_root)
                or source_root.is_relative_to(output_root)):
            raise PatchError('Generated source must be separate from supplied originals')
    destination = relative_file(output_root, recipe['output'])
    result = reconstruct(recipe, roots)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation never overwrites originals or prior runs.
    with destination.open('xb') as stream:
        stream.write(result)
    return {'path': recipe['output'], 'sha256': sha256(result), 'bytes': len(result)}
