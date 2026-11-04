"""Fail-closed code-only export inventory. Does not publish or initialize Git."""
from pathlib import Path
import argparse,hashlib,json,sys
from audit_source_export import audit
ROOT=Path(__file__).resolve().parents[1]
FORBIDDEN={'private','inputs','build','out','outputs','backups','assets','generated','aer-work','avatar-reconstruction','node_modules','.git','.aws','.codex','.claude','__pycache__'}
DENY_PREFIX=('modules/avatar_editor/','third_party/','docs/','modules/xui-runtime/')
ALLOWED_ROOT={'COPYING','Makefile','setup.py','import_trailers.py','.gitignore','README-EXPORT.md','launch_setup.py','ExBox Setup.cmd'}
SUFFIX={'.py','.cpp','.hpp','.h','.c','.sh','.cmd','.json','.toml','.ld','.map','.glsl','.vert','.frag','.comp'}

def permitted(name):
    p=Path(name)
    if p.is_absolute() or '..' in p.parts or any(x in FORBIDDEN for x in p.parts):return False
    if ('av2' in p.name.lower() or 'native_editor' in p.parts or 'avatar_editor' in p.parts) and name!='modules/avatar_editor/UPSTREAM.md':return False
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
        if digest in known:raise ValueError('Known original/derived input bytes: '+name)
        if digest!=row['sha256'] or len(data)!=row['bytes']:raise ValueError('Export changed: '+name)
        if b'\0' in data or data[:4] in (b'XEX2',b'XUIZ',b'\x7fELF',b'PK\x03\x04'):raise ValueError('Binary disguised as code: '+name)
        data.decode('utf-8')

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
