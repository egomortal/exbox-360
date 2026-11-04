"""Resume AV1 from verified staging using only Python's standard library.

python -B tools/avatar360/run.py SOURCE [--all] [--jobs 4]
Outputs are isolated below private/avatar360/av1. No downloaded code is run.
"""
import argparse
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
import hashlib
import json
from pathlib import Path
import sys

from portable import bake, decoder, pose_world, read_png, xforms
from formats import export_glb, write_kit

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from avatar360.convert import FLAGGED, safe_output

CLIPS = [('idle', 'Animation Generic Stand 0'), ('idle2', 'Animation Generic Stand 1'),
         ('idle3', 'Animation Generic Stand 2'), ('wave', 'Animation Generic Wave'),
         ('talk', 'Animation {B} Laugh'), ('dance', 'Animation Generic Celebration'),
         ('interact', 'Animation Generic Clap'), ('jump', 'Animation {B} Shocked Surprised'),
         ('yawn', 'Animation {B} Yawn'), ('look', 'Animation {B} Idle Looks Around'),
         ('shift', 'Animation {B} Idle Shifts Weight'), ('sad', 'Animation {B} Sad Cry'),
         ('angry', 'Animation {B} Angry'), ('confused', 'Animation {B} Confused')]
TOOLS = [Path(__file__), Path(__file__).with_name('portable.py'), Path(__file__).with_name('formats.py'),
         Path(__file__).with_name('review.py'), ROOT/'tools/xavatar.py']


def sha(data): return hashlib.sha256(data).hexdigest()


def dump(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def checked_output(source, output):
    raw = Path(output).absolute()
    for p in (raw, *raw.parents):
        if p.is_symlink() or p.is_junction(): raise ValueError('Output traverses a link or junction')
    source, output = safe_output(Path(source), raw)
    if not output.is_relative_to(ROOT.resolve()): raise ValueError('Output outside workspace')
    return source, output


class Source:
    def __init__(self, source, staged, output):
        self.source, self.output = checked_output(Path(source), Path(output))
        self.staged = Path(staged).resolve()
        self.manifest = json.loads((self.staged/'manifest.json').read_text(encoding='utf-8'))
        if Path(self.manifest['source']).resolve() != self.source:
            raise ValueError('Staging source mismatch')
        self.items = self.manifest['items']
        self.by_index = {r['index']: r for r in self.items}
        self.stock = {r['source_index']: r for r in self.items if r['source_pack'] == self.manifest['stock_pack']}
        self.dec = decoder()
        self.sources = {}
        self.tool_hashes = {str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in TOOLS}
        skeleton = (self.staged/'skeleton_nxe.bin').read_bytes()
        if sha(skeleton) != self.manifest['skeleton']['sha256']: raise ValueError('Skeleton hash mismatch')
        self.read(self.manifest['skeleton']['source'])
        self.joints = self.dec.read_skeleton(skeleton)
        self.xf = xforms(self.joints)
        self.output.mkdir(parents=True, exist_ok=True)

    def read(self, relative, expected=None):
        path = (self.source/relative).resolve()
        if not path.is_relative_to(self.source): raise ValueError('Source path escape')
        data = path.read_bytes(); h = sha(data)
        if expected and h != expected: raise ValueError('Source hash mismatch: '+relative)
        key = path.relative_to(self.source).as_posix()
        if key in self.sources and self.sources[key]['sha256'] != h:
            raise ValueError('Source changed during conversion: '+key)
        self.sources[key] = dict(sha256=h, bytes=len(data))
        return data

    def block(self, record, kind):
        if record.get('guid') in FLAGGED: raise ValueError('Excluded dress')
        self.read('originals/'+record['original_sha256'], record['original_sha256'])
        b = next(b for b in record['blocks'] if b['kind'] == kind)
        self.read(b['path'], b['sha256'])
        self.read('originals/'+b['original_sha256'], b['original_sha256'])
        return b

    def decoded(self, record, kind):
        b = self.block(record, kind)
        return json.loads(self.read('decoded/'+b['sha256']+'/data.json')), b

    def model(self, record):
        data, b = self.decoded(record, 'model')
        images = []
        for i, t in enumerate(data['textures']):
            if t['is_empty']: images.append(None); continue
            im = read_png(self.read('decoded/'+b['sha256']+f'/texture-{i}-0.png'))
            if im[:2] != (t['width'], t['height']): raise ValueError('Source texture dimensions')
            images.append(im)
        return data, images

    def texture(self, record):
        data, b = self.decoded(record, 'texture')
        # Standalone texture data is a single record in the reconstruction.
        if data.get('is_empty'): return None
        return read_png(self.read('decoded/'+b['sha256']+'/texture-0-0.png'))

    def resolve(self, asset_id):
        if int(asset_id, 16) == 0: return None
        if asset_id[16:] != 'c1c8f109a19cb2e0': raise ValueError('Template is not a stock-pack ID')
        idx = int(asset_id[8:12], 16)
        if idx not in self.stock: raise ValueError('Unresolved template ID: '+asset_id)
        r = self.stock[idx]
        if not (int(asset_id[:8], 16) & r['bits']): raise ValueError('Template category mismatch')
        return r

    def part(self, record, model=None, overrides=None, replacements=None, frozen=False):
        model, images = model or self.model(record)
        colors = {22: [1., 1., 1., 1.]}
        color_blocks = [b for b in record['blocks'] if b['kind'] == 'colors']
        if color_blocks:
            b = self.block(record, 'colors'); raw = self.read(b['path'], b['sha256'])
            if len(raw) < 28: raise ValueError('Short color table')
            endian = 'little' if int.from_bytes(raw[:4], 'little') <= 0xffff else 'big'
            cols = [int.from_bytes(raw[4+k*8:8+k*8], endian) for k in range(3)]
            if any(cols):
                for k, c in enumerate(cols): colors[22+k] = [((c>>s)&255)/255 for s in (16, 8, 0)]+[1.]
        colors.update(overrides or {})
        tint = 1 if record['category'] == 'body' else 2 if record['category'] == 'hair' else 0
        prims = []
        for batch in model['batches']:
            if not batch['vertices'] or not batch['indices']: continue
            layer, im = bake(batch, images, colors, replacements, tint=bool(tint) and not frozen)
            flags = {1: 4, 3: 8}.get(batch['shader_id'], 0)
            if record['category'] in ('hair', 'hat') or flags: flags |= 1
            prims.append(dict(vertices=batch['vertices'], indices=batch['indices'], layer=layer,
                              image=im, flags=flags, tint=0 if frozen else tint))
        return dict(name=record['name'], prims=prims)

    def animations(self, xf, body):
        by_name = {r['name']: r for r in self.items if r['group'] == 'stock' and r['category'] == 'animation'}
        clips, records = [], []
        for name, pattern in CLIPS:
            key = pattern.replace('{B}', body)
            if key not in by_name: raise ValueError('Missing required animation: '+key)
            r = by_name[key]; d, _ = self.decoded(r, 'animation')
            if d['pose_counts'][0] != len(self.joints): raise ValueError('Animation rig mismatch')
            clips.append((name, d['fps'], [pose_world(xf, f) for f in d['pose']]))
            records.append(r['index'])
        return clips, records


def apply_shape(dec, model, shape):
    if not dec.shape_fits(shape, model): raise ValueError('Shape does not fit target body/head')
    hides, moves = dec.shape_targets(shape, model)
    for bi, vi, v in moves:
        model['batches'][bi]['vertices'][vi].update({k: v[k] for k in ('pos', 'normal', 'weights', 'joints')})
    return hides


def template(s):
    templates = json.loads(s.read('avatar-manifests.json'))
    t = templates[1]  # Avatar6404: weight=0, height=-0.5; no UV transforms.
    if t['weight_factor'] != 0 or any(x['scale'] or x['rotation'] or any(x['translation']) for x in t['textures']):
        raise ValueError('Selected template needs unsupported proportion/UV transform')
    # Verify the original saved-avatar bytes, not only the decoded summary.
    s.read('originals/'+t['sha256'], t['sha256'])
    body, head = s.resolve(t['body_component']['asset_id_hex']), s.resolve(t['head_component']['asset_id_hex'])
    records = [body, head]+[s.resolve(c['asset_id_hex']) for c in t['components'] if c['categories']]
    if any(not r['bodies'] & body['bodies'] for r in records): raise ValueError('Template body incompatibility')
    models = {r['index']: s.model(r) for r in records}
    hiding, selected = {}, [r['index'] for r in records]
    for asset in t['blend_shapes']:
        r = s.resolve(asset); sh, _ = s.decoded(r, 'shape')
        apply_shape(s.dec, models[head['index']][0], sh); selected.append(r['index'])
    for r in records[2:]:
        if 'hiding_shape' in r:
            sr = s.by_index[r['hiding_shape']]; sh, _ = s.decoded(sr, 'shape')
            for bi, ti in apply_shape(s.dec, models[body['index']][0], sh): hiding.setdefault(bi, set()).add(ti)
            selected.append(sr['index'])
    for bi, hidden in hiding.items():
        b = models[body['index']][0]['batches'][bi]
        b['indices'] = [v for i in range(0, len(b['indices']), 3) if i//3 not in hidden for v in b['indices'][i:i+3]]
    colors = [[((int(c, 16)>>shift)&255)/255 for shift in (16, 8, 0)]+[1.] for c in t['colors_argb']]
    replacements = {}
    for f in t['textures']:
        r = s.resolve(f['asset_id_hex'])
        if r is None: continue
        usage = {'mouth': [12], 'eyes': [9, 10], 'eyebrows': [7, 8], 'eyeshadow': [11], 'facial_hair': [6], 'facial_other': [5]}[r['category']]
        image = s.texture(r)
        for u in usage: replacements[u] = image
        selected.append(r['index'])
        # Native loader auto-binds the eye's original shadow partner.
        if r['category'] == 'eyes' and int(t['colors_argb'][5], 16) & 0xffffff:
            aid = r['source_id']
            if int(aid[:8], 16) == 0x40000:
                shadow = s.stock[int(aid[8:12], 16)]
                replacements[11] = s.texture(shadow); selected.append(shadow['index'])
    parts = []
    for r in records:
        ov = {13+k: c for k, c in enumerate(colors)}
        if r['category'] in ('body', 'hair'): ov[22] = colors[0 if r['category'] == 'body' else 1]
        parts.append(s.part(r, models[r['index']], ov, replacements if r == head else None, frozen=True))
        print('template:', r['name'], flush=True)
    scales = [[1., 1., 1.] for _ in s.joints]
    # Both upstream skeleton scaling versions agree for zero weight and height.
    h = t['height_factor']; factor = min(1., abs(h))
    scales[0] = [1+factor*(.1 if h >= 0 else -.1)]*3
    scales[19] = [1+factor*(-.1 if h >= 0 else .05)]*3
    xf = xforms(s.joints, scales)
    clips, anim_records = s.animations(xf, 'Female')
    review = s.output/'review'; review.mkdir(exist_ok=True)
    kit = review/'Avatar6404.nxak'
    stats = write_kit(kit, s.joints, s.dec.JOINT_NAMES, xf, parts, clips, 'Avatar6404')
    export_glb(review/'Avatar6404.glb', parts, xf, clips[0][2][6])
    # This diagnostic renderer is separate from the unchanged Ex-Box renderer.
    from review import render
    render(review/'Avatar6404.png', parts, xf, clips[0][2][6])
    receipt = dict(template=t, selected=sorted(set(selected)), animations=anim_records, statistics=stats,
                   sources=s.sources, tool_hashes=s.tool_hashes,
                   limitations=['GLB and PNG are a single idle pose; animated rig/clips are in NXAK.',
                                'Material bake cap 256 matches the existing builder; originals retained.',
                                'Facial animation layers, prop triggers and native UI are not reproduced.',
                                'PNG is a Python diagnostic renderer, not a compiled Ex-Box render.'])
    receipt['outputs'] = {p.name: dict(sha256=sha(p.read_bytes()), bytes=p.stat().st_size) for p in review.iterdir() if p.suffix in ('.nxak', '.glb', '.png')}
    dump(review/'template.json', receipt)
    return receipt


def core(s):
    results = {}
    for body in ('Male', 'Female'):
        s.sources = {}
        clips, records = s.animations(s.xf, body)
        path = s.output/'core'/(body.lower()+'.nxak')
        stats = write_kit(path, s.joints, s.dec.JOINT_NAMES, s.xf, [], clips, body)
        results[body] = dict(statistics=stats, animation_records=records, sha256=sha(path.read_bytes()),
                             sources=dict(s.sources))
    dump(s.output/'core/manifest.json', dict(bodies=results, skeleton=s.manifest['skeleton'], tool_hashes=s.tool_hashes))


_worker = None


def worker_init(source, staged, output):
    global _worker
    _worker = Source(source, staged, output)


def convert_item(index):
    s = _worker
    s.sources = {}
    r = s.by_index[index]
    out = s.output/'items'/f'{index:04d}'
    out.mkdir(parents=True, exist_ok=True)
    parts, aux = [], []
    for b in r['blocks']:
        s.block(r, b['kind'])
        if b['kind'] == 'model':
            model = s.model(r)
        elif b['kind'] in ('shape', 'texture'):
            d, _ = s.decoded(r, b['kind'])
            aux.append((b['kind'], d, b))
        else:
            aux.append((b['kind'], None, b))
    previous = out/'receipt.json'
    if previous.exists():
        old = json.loads(previous.read_text(encoding='utf-8'))
        # Resumption is content-addressed; incomplete or changed outputs rebuild.
        same = old.get('record') == r and old.get('tool_hashes') == s.tool_hashes
        for path, info in old.get('sources', {}).items():
            if sha(s.read(path)) != info['sha256']: same = False
        if same and all((out/p).exists() and sha((out/p).read_bytes()) == h['sha256'] for p, h in old.get('outputs', {}).items()):
            return dict(index=index, status='cached', statistics=old['statistics'])
    stats = {}
    if any(b['kind'] == 'model' for b in r['blocks']):
        parts.append(s.part(r, model))
        stats = write_kit(out/'item.nxak', s.joints, s.dec.JOINT_NAMES, s.xf, parts, [], r['name'])
    for kind, data, b in aux:
        if data is not None: dump(out/(kind+'.json'), data)
        else: (out/(kind+'.bin')).write_bytes(s.read(b['path'], b['sha256']))
        if kind == 'texture':
            # Keep every animation layer at full source resolution.
            for layer in range(data['layer_count']):
                if data['is_empty']: continue
                png = s.read('decoded/'+b['sha256']+f'/texture-0-{layer}.png')
                read_png(png)
                (out/f'texture-{layer}.png').write_bytes(png)
    outputs = {p.name: dict(sha256=sha(p.read_bytes()), bytes=p.stat().st_size) for p in out.iterdir() if p.name != 'receipt.json'}
    dump(previous, dict(record=r, sources=s.sources, tool_hashes=s.tool_hashes, statistics=stats, outputs=outputs,
                        note='Independent item shard; body masks, companions and hiding-shape dependencies remain in record.'))
    return dict(index=index, status='converted', statistics=stats)


def convert_checked(index):
    try:
        return convert_item(index)
    except Exception as exc:
        # Keep successful independent shards and report every failed source item.
        return dict(index=index, status='failed', error=f'{type(exc).__name__}: {exc}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--staged', type=Path, default=ROOT/'private/avatar360')
    p.add_argument('--out', type=Path, default=ROOT/'private/avatar360/av1')
    p.add_argument('--all', action='store_true')
    p.add_argument('--skip-template', action='store_true')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--limit', type=int)
    args = p.parse_args()
    if not 1 <= args.jobs <= 16: p.error('--jobs must be 1..16')
    s = Source(args.source, args.staged, args.out)
    core(s)
    if not args.skip_template:
        receipt = template(s); print('template complete', receipt['statistics'], flush=True)
    if args.all:
        report = ROOT/'docs/astra/AV1-avatar-reconstruction.report.md'
        if not report.is_file() or report.stat().st_size < 100: raise ValueError('Write AV1 report before full conversion')
        ids = [r['index'] for r in s.items][:args.limit]
        results = []
        with ProcessPoolExecutor(max_workers=args.jobs, initializer=worker_init,
                                 initargs=(str(s.source), str(s.staged), str(s.output))) as pool:
            for result in pool.map(convert_checked, ids, chunksize=1):
                results.append(result)
                if len(results) % 25 == 0 or len(results) == len(ids):
                    print(f'wardrobe {len(results)}/{len(ids)}', flush=True)
                    dump(s.output/'progress.json', dict(completed=len(results), total=len(ids), last=result['index']))
        failed = [r for r in results if r['status'] == 'failed']
        dump(s.output/'wardrobe.json', dict(items=results, groups=dict(Counter(r['group'] for r in s.items)),
                                           complete=len(ids) == len(s.items) and not failed, failures=failed,
                                           tool_hashes=s.tool_hashes))
        print('wardrobe complete', len(results), flush=True)
        if failed:
            print(json.dumps(failed, indent=2), flush=True)
            raise SystemExit(1)


if __name__ == '__main__': main()
