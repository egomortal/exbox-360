"""Strict custom-scene audit and isolated skin visual recovery (owner files only)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from theme_states import members
from xur5 import D
from classes5 import L as L5
from xur8 import D8
from classes8 import L as L8


def reader(data,build=None):
    version = struct.unpack_from('>I', data, 4)[0]
    def levels(source):
        result=dict(source)
        if build is not None:
            from avatar_schema import properties
            if build in (9199,12581,13604,15574,17559):
                result['XuiAvatar']=result['XuiControl']+[properties(build,version)]
        return result
    if version == 5:
        # Never use the legacy suffix/candidate guesses for dashboard exports.
        return D(data, levels(L5))
    if version in (7, 8):
        return D8(data, levels(L8))
    raise ValueError('Unsupported XUIB version %d' % version)


def decode(data):
    d = reader(data)
    return d.decode(), d


def walk(node):
    yield node
    for child in node.get('children', []):
        yield from walk(child)


def isolated(data, identity, cls='XuiVisual'):
    """T10's isolated-group approach, accepting one complete typed subtree.

    Prefer full DATA traversal; isolated scanning remains for unrelated skins
    with unknown ancestors. Unsupported curves stay marked on every track.
    """
    # Full traversal supplies shared property/compound context (13604 uses
    # earlier gradient objects in equalizer_small). Prefer verified DATA.
    d = reader(data)
    try:
        tree = d.decode()
    except (AssertionError, KeyError, IndexError, ValueError, struct.error):
        pass
    else:
        matches = [n for n in walk(tree) if n['class'] == cls and n.get('props', {}).get('Id') == identity]
        if len(matches) != 1:
            raise ValueError('Expected one %s in verified DATA, found %d' % (identity, len(matches)))
        node = matches[0]
        return node, d, *d.ranges[id(node)]
    index = d.strings.index(cls)
    version = struct.unpack_from('>I', data, 4)[0]
    def levels(source):
        result=dict(source)
        if build is not None:
            from avatar_schema import properties
            if build in (9199,12581,13604,15574,17559):
                result['XuiAvatar']=result['XuiControl']+[properties(build,version)]
        return result
    if version == 5:
        signature = struct.pack('>H', index)
    else:
        if index >= 240:
            raise ValueError('Isolated class index requires a packed signature')
        signature = bytes([index])
    offset, size = d.parts['DATA']
    matches, failures = [], []
    for at in range(offset, offset + size - len(signature)):
        if data[at:at+len(signature)] != signature:
            continue
        # Fresh state: a failed v8 candidate cannot pollute shared-property ids.
        candidate = reader(data)
        candidate.at = at
        try:
            node = candidate.obj()
            if node.get('props', {}).get('Id') == identity and candidate.at <= offset+size:
                matches.append((node, candidate, at, candidate.at))
        except (AssertionError, KeyError, IndexError, ValueError, struct.error) as error:
            objects = getattr(candidate, 'objects', [])
            if objects and objects[0].get('props', {}).get('Id') == identity:
                failures.append(dict(offset=at, blocker=str(error)))
    if len(matches) != 1:
        raise ValueError('Expected one isolated %s, found %d; %s' % (identity, len(matches), failures))
    return matches[0]


def sources(root):
    for package in sorted(Path(root).rglob('*')):
        if package.name not in ('slots', 'controlp', 'music') or not package.is_file():
            continue
        for name, data in members(package.read_bytes()):
            if ((package.name == 'slots' and name.endswith('SlotScene.xur')) or
                    name.lower() in ('aurascene.xur', 'legendscene.xur', 'panelscene.xur', 'equalizer.xur')):
                yield package, name, data


def audit(root):
    results, cache = [], {}
    for package, name, data in sources(root):
        digest = hashlib.sha256(data).hexdigest()
        if digest not in cache:
            try:
                tree, d = decode(data)
                opaque = sorted({p for n in walk(tree) for p in n.get('props', {}) if p.startswith('Undecoded')})
                cache[digest] = dict(decoded=True, data_end=d.at, opaque_properties=opaque)
            except (AssertionError, KeyError, IndexError, ValueError, struct.error) as error:
                cache[digest] = dict(decoded=False, blocker=str(error))
        results.append(dict(package=package.relative_to(root).as_posix(), member=name,
                            sha256=digest, version=struct.unpack_from('>I', data, 4)[0], **cache[digest]))
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if 'private' not in args.output.resolve().parts:
        parser.error('Audit evidence belongs under private/')
    rows = audit(args.root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8')
    print('Copies:', len(rows), 'decoded:', sum(r['decoded'] for r in rows),
          'unique:', len({r['sha256'] for r in rows}))
