"""Build the private disk catalog and preserve head/layout data needed by AV2."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET
from run import Source, dump, sha


def prepare(source, staged, output):
    s = Source(source, staged, output)
    wardrobe = json.loads((s.output/'wardrobe.json').read_text(encoding='utf-8'))
    if not wardrobe['complete']: raise ValueError('Complete the wardrobe conversion first')
    catalog, layouts = [], {}
    for r in s.items:
        folder = s.output/'items'/f'{r["index"]:04d}'
        receipt = json.loads((folder/'receipt.json').read_text(encoding='utf-8'))
        cat = {k: r[k] for k in ('index', 'source_pack', 'source_index', 'group', 'guid', 'name', 'category',
                                 'bodies', 'bits', 'original_sha256', 'source_id', 'partner_only', 'dependencies')}
        cat.update(companion=r.get('companion'), hiding_shape=r.get('hiding_shape'),
                   receipt=f'items/{r["index"]:04d}/receipt.json', statistics=receipt['statistics'],
                   fidelity='converted; not individually visually verified')
        catalog.append(cat)
        if any(b['kind'] == 'model' for b in r['blocks']):
            model, b = s.decoded(r, 'model')
            prim = 0; batches = []
            for batch in model['batches']:
                nonempty = bool(batch['vertices'] and batch['indices'])
                batches.append(dict(vertex_size=batch['vertex_size'], vertices=len(batch['vertices']),
                                    triangles=batch['triangle_count'], shader=batch['shader_id'],
                                    exported_primitive=prim if nonempty else None))
                if nonempty: prim += 1
            layouts[str(r['index'])] = dict(model_sha256=b['sha256'], batches=batches)
            if r['category'] == 'head':
                dump(s.output/'core/head-model.json', model)
    dump(s.output/'catalog.json', catalog)
    dump(s.output/'core/model-layouts.json', layouts)
    # Stream metadata by fixed offsets; do not parse the whole Marketplace JSON
    # into the future runtime's heap. This desktop conversion can read it once.
    market_report = json.loads(s.read('marketplace/import-report.json'))
    accepted = [r for r in market_report['decisions'] if r['status'] == 'staged']
    offsets = []
    with (s.output/'marketplace.jsonl').open('wb') as f:
        for r in accepted:
            data = json.dumps(r, ensure_ascii=False, separators=(',', ':')).encode('utf-8')+b'\n'
            offsets.append((f.tell(), len(data))); f.write(data)
    (s.output/'marketplace.offsets').write_bytes(b'AVIX'+struct.pack('<II', 1, len(offsets))
                                               +b''.join(struct.pack('<QI', o, n) for o, n in offsets))
    # Keep extracted proprietary UI text in private, not in tools or public docs.
    ui = 'editor-source/12581/unpacked/MEDIA/'
    strings = s.read(ui+'Strings.xus')
    evidence = dict(strings_format='XUIS UTF-8; fragments only, not an ID table decoder',
                    string_fragments=[x.decode('ascii') for x in re.findall(rb'[\x20-\x7e]{4,}', strings)], scenes={})
    for name in ('EditorMain.xui', 'EditorStart.xui', 'EditorMarketplace.xui'):
        data = s.read(ui+name); root = ET.fromstring(data)
        evidence['scenes'][name] = [dict(tag=n.tag, value=n.text) for n in root.iter()
                                   if n.tag in ('Id', 'Text', 'ImagePath', 'ClassOverride', 'DefaultFocus')]
    for name in ('GridMain.xur.json', 'Grid.xur.json', 'HeightAndWidth.xur.json', 'Photobooth.xur.json', 'DialogPhotobooth.xur.json'):
        evidence['scenes'][name] = json.loads(s.read(ui+name))
    dump(s.output/'ui-reference.json', evidence)
    dump(s.output/'catalog-provenance.json', dict(sources=s.sources, stock_records=len(catalog),
                                                marketplace_records=len(accepted),
                                                tool_sha256=sha(Path(__file__).read_bytes())))
    print('catalog', len(catalog), 'Marketplace', len(accepted), 'layouts', len(layouts))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source'); p.add_argument('--staged', default='private/avatar360')
    p.add_argument('--out', default='private/avatar360/av1')
    a = p.parse_args(); prepare(a.source, a.staged, a.out)
