"""Reproduce T15 byte evidence from read-only owner packages into private/."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from custom_scenes import reader, sources, members, walk
from compile_custom_scenes import BUILDS
from xur8 import D8


def inspect(data):
    d = reader(data)
    blocks = []
    original = d.block

    def block(table, out):
        start = d.at
        if isinstance(d, D8):
            mask = d.packed()
        else:
            width = d.u8() & 7
            mask = int.from_bytes(data[d.at:d.at+width], 'big')
        d.at = start
        count = original(table, out)
        fields = {str(bit): dict(name=name, type=kind, value=out[name])
                  for bit, (name, kind) in ((b, v) for b, v in table.items() if b != '__name__')
                  if mask >> bit & 1}
        blocks.append(dict(level=table.get('__name__'), mask=hex(mask), count=count,
                           start=start, end=d.at, bytes=data[start:d.at].hex(), fields=fields))
        return count

    d.block = block
    tree = d.decode()
    result = dict(version=struct.unpack_from('>I', data, 4)[0], sections=d.parts,
                  data_end=d.at, blocks=blocks,
                  nodes=[dict(cls=n['class'], identity=n.get('props', {}).get('Id'),
                              byte_range=d.ranges[id(n)]) for n in walk(tree)])
    result['additional_curve_or_sound_tracks'] = [dict(owner=n.get('props', {}).get('Id'), track=t)
                                                  for n in walk(tree) for t in n.get('timelines', {}).get('tracks', [])
                                                  if 'UndecodedSound0' in t['props'] or t.get('v7_curve_parameters')]
    if isinstance(d, D8):
        result.update(header_counts=d.counts, observed_counts=d.got)
    if isinstance(d, D8) and d.version == 8:
        records = []
        start, size = d.parts.get('KEYD', (0, 0))
        d.at = start
        while d.at < start+size:
            begin = d.at
            frame, flag = d.packed(), d.byte()
            kind = flag & 63
            ext = [d.byte(), d.byte(), d.byte()] if kind == 2 else [d.packed()] if kind == 10 else []
            index = d.packed()
            records.append(dict(start=begin, end=d.at, bytes=data[begin:d.at].hex(),
                                frame=frame, flag=flag, extension=ext, keyp_index=index))
        assert [(r['frame'], r['flag'], r['extension'], r['keyp_index']) for r in records] == d.keyd
        result['extended_key_records'] = [dict(record=r, previous=records[i-1] if i else None,
                                               following=records[i+1] if i+1 < len(records) else None)
                                          for i, r in enumerate(records) if r['flag'] & 63 in (5, 6, 8)]
        result['extended_key_tracks'] = [dict(owner=n.get('props', {}).get('Id'), track=t)
                                         for n in walk(tree) for t in n.get('timelines', {}).get('tracks', [])
                                         if any(f & 63 in (5, 6, 8) for f, _ in t.get('flags', []))]
    return tree, result


def evidence(root):
    corpus = {}
    for package, name, data in sources(root):
        digest = hashlib.sha256(data).hexdigest()
        corpus.setdefault(digest, (package.relative_to(root).as_posix(), name, data))
    for build, relative in BUILDS.items():
        package = root/relative/'dash.xex/dashuisk'
        data = dict(members(package.read_bytes()))['skin.xur']
        corpus[hashlib.sha256(data).hexdigest()] = (package.relative_to(root).as_posix(), 'skin.xur', data)
    records, trees = [], {}
    for digest, (package, name, data) in corpus.items():
        row = dict(sha256=digest, package=package, member=name)
        try:
            tree, facts = inspect(data)
            row.update(decoded=True, **facts)
            trees[digest] = json.dumps(tree, sort_keys=True)
        except (AssertionError, KeyError, IndexError, ValueError, struct.error) as error:
            row.update(decoded=False, blocker=str(error))
        records.append(row)
    comparisons = []
    for digest, (package, name, data) in corpus.items():
        if data[7] != 7 or digest not in trees:
            continue
        for other, (op, on, ob) in corpus.items():
            if ob[7] == 7 or on != name or other not in trees:
                continue
            if trees[digest] == trees[other]:
                comparisons.append(dict(v7=digest, other=other, other_version=ob[7],
                                        identical_decoded_tree=True))
    return dict(sources=records, identical_cross_version_trees=comparisons)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if 'private' not in args.output.resolve().parts:
        parser.error('Owner byte evidence belongs under private/')
    result = evidence(args.root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Unique scenes/skins:', len(result['sources']), 'decoded:', sum(r['decoded'] for r in result['sources']),
          'identical cross-version trees:', len(result['identical_cross_version_trees']))
