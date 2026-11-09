"""Read the owner's reconstruction; build separate private NXAK v2 kits.

No source writes, downloads, native editor launches or runtime modifications.
Uses the existing Ex-Box decoders/baker without changing those files.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import avatar_unpack as au

FLAGGED = {
    '00800018-55bc-0002-930f-f6df1ed8bf7f',
    '00800018-817f-0002-1d58-022f6c85ff8b',
    '00800018-8d36-0002-cadc-c209f2c41093',
    '00800018-9ad5-0002-f6a4-adf0fd118784',
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def safe_output(source, output):
    source, output = source.resolve(), output.resolve()
    private = (ROOT / 'private/avatar360').resolve()
    if not output.is_relative_to(private) or output.is_relative_to(source):
        raise ValueError('Output must be under this workspace private/avatar360, outside the source')
    if any(p.is_symlink() or (hasattr(p, 'is_junction') and p.is_junction())
           for p in [output, *output.parents] if p.is_relative_to(private)):
        raise ValueError('Output may not traverse a link or junction')
    return source, output


def stage(source, output):
    source, output = safe_output(source, output)
    closet = read_json(source / 'editor/closet-import-report.json')
    hair = read_json(source / 'editor/hair-integration-report.json')
    market = read_json(source / 'marketplace/import-report.json')
    recovery = read_json(source / 'recovery-results.json')
    packs = {r['sha256']: {i['index']: i for i in r['items']} for r in recovery
             if r['items'] and r['items'][0].get('index') is not None}
    stock = closet['active_pack']
    records = []
    by_key = {}
    sources = {}

    def verified(relative, expected=None):
        path = (source / relative).resolve()
        if not path.is_relative_to(source):
            raise ValueError('Source path escapes reconstruction')
        data = path.read_bytes()
        sha = digest(data)
        if expected is not None and sha != expected:
            raise ValueError('Source hash mismatch: ' + str(relative))
        sources[str(path.relative_to(source))] = {'sha256': sha, 'bytes': len(data)}
        return data

    for name in ['recovery-results.json', 'decoded-catalog.json', 'avatar-manifests.json',
                 'editor/closet-import-report.json', 'editor/hair-integration-report.json',
                 'editor/assets/closet/closet_index.tsv', 'marketplace/import-report.json']:
        verified(name)

    def add(pack, index, group, guid=None, partner=False):
        key = (pack, index)
        if key in by_key:
            return by_key[key]
        r = packs[pack][index]
        ri = len(records)
        rec = {'index': ri, 'source_pack': pack, 'source_index': index,
               'group': group, 'guid': guid, 'name': r['english'] or r['internal'],
               'category': r['category'], 'bodies': r['bodies'], 'bits': r['categories'],
               'original_sha256': r['original_sha256'], 'source_id': r['id'],
               'blocks': [], 'partner_only': partner, 'dependencies': []}
        records.append(rec)
        by_key[key] = ri
        verified('originals/' + r['original_sha256'], r['original_sha256'])
        for b in r['blocks']:
            if b['status'] != 'unpacked':
                raise ValueError('Unresolved source block: ' + str(key))
            if b['kind'] not in ('model', 'texture', 'animation', 'shape', 'colors'):
                continue
            data = verified(b['path'], b['sha256'])
            verified('originals/' + b['original_sha256'], b['original_sha256'])
            # Use unique numeric filenames; original labels stay in the manifest.
            path = output / 'staged' / r['category'] / f'{ri:04d} item-{ri}.{b["kind"]}.bin'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            rec['blocks'].append({**b, 'output': str(path.relative_to(output))})
        return ri

    for i, r in packs[stock].items():
        if r['blocks']:
            add(stock, i, 'stock', partner='(Hat)' in (r['english'] or ''))
    excluded = []
    for d in closet['decisions']:
        if d['status'] != 'imported':
            continue
        if d['guid'] in FLAGGED:
            excluded.append({**d, 'reason': 'Confirmed neckline defect; TEST-REPORT-20261028.md'})
        else:
            add(d['pack'], d['index'], 'closet', d['guid'])
    for d in hair['decisions']:
        if d['status'] != 'appended_pair':
            continue
        a = add(d['source_pack'], d['source_index'], 'hair')
        b = add(d['source_pack'], d['source_partner'], 'hair-companion', partner=True)
        records[a]['companion'] = b
    # Rebase links into this staged index, never into another revision's pack.
    pos = 0
    while pos < len(records):
        r = records[pos]
        aid = r['source_id']
        first, target = int(aid[:8], 16), int(aid[8:12], 16)
        if first == 0x01000000:
            ri = add(r['source_pack'], target, 'hiding-shape')
            if not any(b['kind'] == 'shape' for b in records[ri]['blocks']):
                raise ValueError('Missing linked hiding shape')
            r['hiding_shape'] = ri
            r['dependencies'].append(ri)
        if r['category'] == 'hair' and not r['partner_only']:
            if 'companion' not in r:
                partner = packs[r['source_pack']].get(target)
                if partner and partner['category'] == 'hair' and target != r['source_index']:
                    r['companion'] = add(r['source_pack'], target, 'hair-companion', partner=True)
            if 'companion' in r:
                r['dependencies'].append(r['companion'])
        pos += 1
    with (output / 'staged/index.tsv').open('w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['index', 'category', 'bits', 'bodies', 'id', 'blocks', 'internal', 'english'])
        for r in records:
            aid = r['source_id']
            target = r.get('hiding_shape', r.get('companion'))
            if target is not None:
                aid = aid[:8] + f'{target:04x}' + aid[12:]
            # The old builder derives filenames from labels; use internal names
            # for filenames, restore original labels after conversion.
            label = f'item-{r["index"]}'
            if r['partner_only']:
                label += ' (Hat)'
                for b in r['blocks']:
                    old = output / b['output']
                    new = old.with_name(old.name.replace(f'item-{r["index"]}.', label + '.'))
                    # No deletions or moves: retain the initial staged copy too.
                    new.write_bytes(old.read_bytes())
            # Bodies and animations have semantic names in the original builder.
            if r['category'] in ('body', 'animation'):
                label = r['name']
                for b in r['blocks']:
                    old = output / b['output']
                    safe = re.sub(r'[\\/:*?"<>|]', '_', label)[:80]
                    new = old.with_name(f'{r["index"]:04d} {safe}.{b["kind"]}.bin')
                    new.write_bytes(old.read_bytes())
            r['builder_label'] = label
            w.writerow([r['index'], r['category'], f'{r["bits"]:x}', r['bodies'], aid,
                        ','.join(b['kind'] for b in r['blocks']) or '-', label, label])
    skeleton_path = 'references/AvatarEditorRecomp/src/kernel/xam/avatars/skeleton_data.cpp'
    text = verified(skeleton_path).decode('utf-8')
    array = re.search(r'static uint8_t skeleton_nxe\[\]\s*=\s*\{(.*?)\};', text, re.S)
    if not array:
        raise ValueError('Embedded NXE skeleton not found')
    skeleton = bytes(int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]{2})', array[1]))
    (output / 'skeleton_nxe.bin').write_bytes(skeleton)
    dump(output / 'exclusions.json', {'dresses': excluded,
         'held_packages': [d for d in market['decisions'] if d['status'] == 'held'],
         'duplicates': [d for d in market['decisions'] if d['status'] == 'duplicate_product']})
    dump(output / 'marketplace-index.json', [d for d in market['decisions'] if d['status'] == 'staged'])
    dump(output / 'templates.json', read_json(source / 'avatar-manifests.json'))
    manifest = {'schema': 1, 'source': str(source), 'stock_pack': stock, 'items': records,
                'sources': sources, 'skeleton': {'source': skeleton_path, 'sha256': digest(skeleton)},
                'excluded_dresses': len(excluded), 'hair_pairs': hair['new_hairstyle_pairs'],
                'note': 'Private conversion; marketplace metadata only, not resident geometry.'}
    dump(output / 'manifest.json', manifest)
    print(f'Staged {len(records)} records; verified {len(sources)} source files', flush=True)
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--out', type=Path, default=ROOT / 'private/avatar360')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--stage-only', action='store_true')
    p.add_argument('--finish-only', action='store_true')
    args = p.parse_args()
    source, output = safe_output(args.source, args.out)
    if not args.finish_only:
        stage(source, output)
        if args.stage_only:
            return
        subprocess.run([sys.executable, '-B', str(ROOT / 'tools/build_avatar_kit360.py'),
                        str(output / 'staged'), str(output / 'skeleton_nxe.bin'),
                        str(output / 'archive.nxak'), '--jobs', str(args.jobs)], check=True)
    from finish import finish
    finish(source, output)


if __name__ == '__main__':
    main()
