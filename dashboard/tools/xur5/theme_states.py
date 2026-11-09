"""Decode an isolated Aura Color group without guessing its custom ancestors.

All properties, children and typed tracks must parse with the established XUI
schema. Only one candidate with all 17 named stops is accepted. This does not
claim that a scene state is an installed retail theme or a flat background.
"""
import json
import pathlib
import struct
import sys

from xur5 import D
from classes5 import L as L5
from xur8 import D8
from classes8 import L as L8


# Public members() keeps original names/order; no payload conversion.
from xuiz_archive import members


def decode(data):
    version = struct.unpack_from('>I', data, 4)[0]
    if version not in (5, 7, 8):
        return {'error': 'Unsupported XUIB version %d' % version}
    ctor, levels = (D, L5) if version == 5 else (D8, L8)
    reader = ctor(data, levels)
    group = reader.strings.index('XuiGroup')
    offset, size = reader.parts['DATA']
    expected = ['Default'] + ['Color%d' % i for i in range(16)]
    candidates, errors = [], []
    for at in range(offset, offset+size-3):
        if version == 5:
            if data[at:at+3] != struct.pack('>HB', group, 7):
                continue
        else:
            # This scene's XuiGroup index fits a single packed byte.
            assert group < 240
            if data[at:at+2] != bytes([group, 7]):
                continue
        reader = ctor(data, levels)
        reader.at = at
        try:
            node = reader.obj()
            if node.get('props', {}).get('Id') != 'Color':
                continue
            named = node.get('timelines', {}).get('named', [])
            assert [n[0] for n in named] == expected
            assert all(n[1] == i and n[2] == 1 for i, n in enumerate(named))
            assert reader.at <= offset+size
            candidates.append({'offset': at, 'end': reader.at, 'group': node})
        except (AssertionError, KeyError, IndexError, ValueError, struct.error) as error:
            errors.append({'offset': at, 'error': str(error)})
    if len(candidates) != 1:
        return {'verified': False, 'candidates': len(candidates), 'failures': errors}
    return {'verified': True, **candidates[0]}


if __name__ == '__main__':
    root, destination = map(pathlib.Path, sys.argv[1:3])
    results, cache = [], {}
    for source in root.rglob('controlp'):
        if not source.is_file():
            continue
        for name, data in members(source.read_bytes()):
            if name.lower() != 'aurascene.xur':
                continue
            if data not in cache:
                try:
                    cache[data] = decode(data)
                except (AssertionError, KeyError, ValueError, struct.error) as error:
                    cache[data] = {'verified': False, 'error': str(error)}
            results.append({'source': str(source), **cache[data]})
    destination.write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('Copies:', len(results), 'unique:', len(cache),
          'verified:', sum(bool(r.get('verified')) for r in results))
    for result in cache.values():
        if not result.get('verified'):
            print(result)
