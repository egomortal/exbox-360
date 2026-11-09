"""Fail-closed code-only export inventory. Does not publish or initialize Git."""
from pathlib import Path
import argparse,hashlib,json,sys,re
from audit_source_export import audit
ROOT=Path(__file__).resolve().parents[1]
FORBIDDEN={'private','inputs','build','out','outputs','backups','assets','generated','aer-work','avatar-reconstruction','node_modules','.git','.aws','.codex','.claude','__pycache__','upscaled','originals','user-data','upscale-runtime','build-work'}
DENY_PREFIX=('modules/avatar_editor/','third_party/','docs/','modules/xui-runtime/')
ALLOWED_ROOT={'COPYING','Makefile','setup.py','import_trailers.py','.gitignore','README-EXPORT.md','launch_setup.py','ExBox Setup.cmd','THIRD_PARTY_NOTICES.md','docs/UPSCALER-CHOICE.md','licenses/Real-ESRGAN-LICENSE.txt','licenses/ncnn-LICENSE.txt'}
SUFFIX={'.py','.cpp','.hpp','.h','.c','.sh','.cmd','.json','.toml','.ld','.map','.glsl','.vert','.frag','.comp'}

REVIEWED_SUPPLEMENT={'modules/avatar_editor/native/return_ticket.cpp': '472ca8badb2ebd7c461a08145e4974504b25f9891b349d37a2c779a86409b1ad', 'modules/avatar_editor/native/return_ticket.h': '6b6fff88ea50d6c241f41f97404ca45e371af03b83548d164508edf446d297e7', 'modules/avatar_editor/native/ticket_file.cpp': 'f46e9a413e027653feff540c993f266a94c3905e6d2a6b7d0f18d667ac86d327', 'modules/avatar_editor/native/ticket_file.h': '430882ef27d3f07aed5a61f6d448be5493709b32f6f30046f01827a73aa42967', 'modules/avatar_editor/tests/return_ticket_test.cpp': 'c22cdafcbd33024528e7b95945b75e1b8e8a3a88120f2ab96218d8f9a78f1466', 'modules/avatar_editor/tests/ticket_file_test.cpp': '93f1004793c0ceed80b0222ed4165949d60424dec0600b1cb77e433b21bf5974', 'tools/avatar_editor/stage.py': '56218c4f4911bd01cd22d78f59b2ceccb9d2ff777d4bb027c1b06d8c6c8c077e', 'tools/build_av2_asset_index.py': 'ac223a2caffe195a978859d6433f665f7b36dd525bce42db06820816f0cf27f6', 'vendor/audio/NOTICE-EXBOX.md': '88ca8a009626909f780ec934f03bfc01668d563604206e3dd7967ea645b151d2', 'vendor/audio/SHA256SUMS': '0c718f5545f0b1dc7266568919d34e509cfceab4f42f687cfa4f83f476528fdb'}

ALLOWED_ROOT.add("README-NATIVE.md")

def permitted(name):
    p=Path(name)
    if p.is_absolute() or '..' in p.parts or any(x in FORBIDDEN for x in p.parts):return False
    if name in REVIEWED_SUPPLEMENT:return True
    if ('av2' in p.name.lower() or 'native_editor' in p.parts or 'avatar_editor' in p.parts) and name!='modules/avatar_editor/UPSTREAM.md':return False
    if name in ALLOWED_ROOT:return True
    if name.startswith(DENY_PREFIX):return name=='modules/avatar_editor/UPSTREAM.md'
    if name.endswith(('.exe','.elf','.bin','.pack','.xex','.xzp','.png','.jpg','.wav','.mp4','.ttf','.spv')):return False
    if p.name.startswith('.env'):return False
    return name in ALLOWED_ROOT or p.suffix in SUFFIX

def verify(root,rows,known):
    seen=set()
    for row in rows:
        name=row['path'];p=root/name
        if name in seen or not permitted(name):raise ValueError('Excluded/duplicate export path: '+name)
        seen.add(name)
        if p.is_symlink() or any(x.is_symlink() for x in p.parents):raise ValueError('Symlink export: '+name)
        data=p.read_bytes();digest=hashlib.sha256(data).hexdigest()
        if name in REVIEWED_SUPPLEMENT and digest!=REVIEWED_SUPPLEMENT[name]:raise ValueError('Reviewed supplemental source changed: '+name)
        if digest in known:raise ValueError('Known original/derived input bytes: '+name)
        if digest!=row['sha256'] or len(data)!=row['bytes']:raise ValueError('Export changed: '+name)
        if b'\0' in data or data[:4] in (b'XEX2',b'XUIZ',b'\x7fELF',b'PK\x03\x04'):raise ValueError('Binary disguised as code: '+name)
        data.decode('utf-8')

def verify_tree(root,rows,known):
    root=Path(root).resolve(strict=True)
    actual=set()
    for p in root.rglob('*'):
        if p.is_symlink():raise ValueError('Symlink in staged export: '+str(p))
        if p.is_file():actual.add(p.relative_to(root).as_posix())
    expected={row['path'] for row in rows}
    if actual!=expected:raise ValueError('Unlisted or missing files in staged export: '+str(sorted(actual^expected)))
    verify(root,rows,known)
    for row in rows:
        data=(root/row['path']).read_bytes()
        if data.startswith((b'\x89PNG\r\n\x1a\n',b'\xff\xd8\xff',b'GIF87a',b'GIF89a',b'BM',b'II*\x00',b'MM\x00*')) or (data[:4]==b'RIFF' and data[8:12]==b'WEBP'):
            raise ValueError('Image disguised as source: '+row['path'])
        if re.search(rb'data:image/[A-Za-z0-9.+-]+;base64,[A-Za-z0-9+/=]{128,}',data) or re.search(rb'iVBORw0KGgo[A-Za-z0-9+/=]{128,}',data):raise ValueError('Embedded image bytes in staged export: '+row['path'])
        start=data[:512].lstrip().lower()
        if start.startswith(b'<svg') or (start.startswith(b'<?xml') and b'<svg' in start):raise ValueError('Vector art in staged export: '+row['path'])

def known_hashes(root):
    hashes=set()
    for registry,key in [('known-modules.json','modules'),('known-updates.json','archives')]:
        for row in json.loads((root/'tools/population'/registry).read_text())[key]:
            value=row.get('sha256',row.get('archive_sha256'))
            if value:hashes.add(value)
    inventory=root/'private/bootstrap-continuation-20261102/asset-inventory.jsonl'
    if inventory.exists():
        for line in inventory.open(encoding='utf-8'):
            row=json.loads(line)
            if row.get('sha256'):hashes.add(row['sha256'])
    return hashes

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=ROOT/'private/gh1-20261102/export-manifest.json');args=parser.parse_args()
    report=audit(ROOT);known=known_hashes(ROOT);rows=[x for x in report['source_candidates'] if permitted(x['path']) and x['sha256'] not in known]
    verify(ROOT,rows,known)
    result=dict(schema=1,licence='GPL-3.0-or-later',status='review inventory; not a standalone build or publication approval',files=rows,known_input_hashes=len(known),review_required=report['review_required'],external_dependencies=report['external_dependencies'])
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    top=[]
    for p in sorted(ROOT.iterdir()):
        matched=[x for x in rows if x['path']==p.name or x['path'].startswith(p.name+'/')]
        kind='include reviewed code subset' if matched else 'exclude / retain privately'
        reason='Source inventory only; see export-manifest.json for exact paths' if matched else 'Original/generated/build/private data or unreviewed provenance; no implicit inclusion'
        top.append(f'| `{p.name}` | {kind} | {len(matched)} | {reason} |')
    (args.output.parent/'top-level-paths.md').write_text('| Path | Decision | Files | Reason |\n|---|---|---:|---|\n'+'\n'.join(top)+'\n')
    print(json.dumps({'files':len(rows),'known_input_hashes':len(known),'review_required':len(report['review_required']),'output':str(args.output)}))
if __name__=='__main__':main()
