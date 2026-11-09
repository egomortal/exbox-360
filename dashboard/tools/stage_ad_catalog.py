"""Stage preserved archived imagery for explicit native per-era ad selection."""
from pathlib import Path
import argparse, collections, hashlib, json, re, shutil
from PIL import Image

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def clean(value):
    return re.sub(r'[\t\r\n]+',' ',str('' if value is None else value)).strip()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--audit',type=Path,required=True)
    p.add_argument('--roles',type=Path,required=True)
    a=p.parse_args();a.assets=a.assets.resolve();a.output=a.output.resolve();a.audit=a.audit.resolve()
    source=a.assets/'promotions';a.output.mkdir(parents=True,exist_ok=True);(a.output/'images').mkdir(exist_ok=True)
    catalog=json.loads((source/'catalog.json').read_text(encoding='utf-8'))
    evidence=json.loads((source/'slot-evidence.json').read_text(encoding='utf-8'))
    roles=json.loads(a.roles.read_text(encoding='utf-8'))['records']
    by_hash=collections.defaultdict(list)
    for entry in evidence:by_hash[entry['sha256']].append(entry)
    previews={}
    for playlist in sorted(a.output.glob('build-*.tsv')):
        for line in playlist.read_text(encoding='utf-8').splitlines():
            fields=line.split('\t')
            if len(fields)>=3 and fields[2]:previews[fields[1].split('.')[0]]=fields[2]
    records=[];identities=set();created=[]
    for entry in catalog:
        original=source/entry['file'];identity=digest(original)
        assert identity==entry['sha256'],('Changed preserved source',str(original))
        assert identity not in identities,('Duplicate source identity',identity)
        identities.add(identity)
        with Image.open(original) as image:
            assert (image.width,image.height)==(entry['width'],entry['height'])
        filename=identity[:20]+original.suffix.lower();target=a.output/'images'/filename
        existed=target.exists()
        if existed:assert digest(target)==identity,('Destination collision or changed source bytes',str(target))
        else:shutil.copy2(original,target);created.append(str(target))
        assert digest(target)==identity
        slots=by_hash[identity]
        slot=next((s for s in slots if s.get('caption')),next(iter(slots),{}))
        title=clean(slot.get('caption') or entry.get('title') or Path(entry.get('original_name') or entry['file']).stem)
        subtitle=clean(slot.get('subtitle') or entry.get('purpose'))
        feed_caption=clean(slot.get('caption'))
        caption_source='original-feed' if feed_caption and not re.fullmatch(r'(?:[A-Za-z0-9_-]+:)?0x[0-9A-Fa-f]+',feed_caption) else 'official-store-title' if entry.get('title') else 'archive-filename'
        kind=entry['kind'];classification='official-store-artwork' if kind=='official-store-artwork' else 'original-dashboard-image'
        role=roles[identity]
        records.append(dict(id=identity,title=title,image=filename,preview=previews.get(identity[:20],''),subtitle=subtitle,
            classification=classification,width=entry['width'],height=entry['height'],source_kind=kind,
            role=role['role'],auto_eligible=int(role['automatic_pool_eligible']),preferred_fit=role['preferred_fit'],
            caption_source=caption_source,
            role_confidence=role['confidence'],role_evidence=role['evidence'],
            original_source=str(original),source_url=entry['source'],original_name=entry.get('original_name'),
            original_resolution_preserved=True,original_bytes_preserved=True,recreated=False,
            manifest_slot_association_verified=bool(slots) and classification!='official-store-artwork',
            slot_evidence=slots,placement_policy='Manual selection can override era; original per-build playlist remains separate.'))
    records.sort(key=lambda r:(r['title'].casefold(),r['id']))
    rows=['\t'.join(clean(r[k]) for k in ['id','title','image','preview','subtitle','classification','width','height','role','auto_eligible','preferred_fit','caption_source']) for r in records]
    assert all(len(row.encode('utf-8'))<4096 and len(row.split('\t'))==12 for row in rows)
    catalog_path=a.output/'catalog.tsv'
    if catalog_path.exists():
        old=catalog_path.read_bytes();backup=a.audit/'backups'/hashlib.sha256(old).hexdigest()/'catalog.tsv'
        backup.parent.mkdir(parents=True,exist_ok=True)
        if not backup.exists():backup.write_bytes(old)
        assert digest(backup)==hashlib.sha256(old).hexdigest()
    catalog_path.write_text('\n'.join(rows)+'\n',encoding='utf-8')
    (a.output/'catalog-provenance.json').write_text(json.dumps(dict(schema=1,records=records,
        role_source=str(a.roles.resolve()),role_source_sha256=digest(a.roles),
        classification_note='Original dashboard imagery includes campaigns, service artwork, backgrounds, logos and placeholders. Official store art is separate and is not an original dashboard advertisement.'),indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    a.audit.mkdir(parents=True,exist_ok=True)
    report=dict(catalog_source=str(source/'catalog.json'),catalog_source_sha256=digest(source/'catalog.json'),
        catalog_path=str(catalog_path),catalog_sha256=digest(catalog_path),entries=len(records),
        classifications=dict(collections.Counter(r['classification'] for r in records)),
        roles=dict(collections.Counter(r['role'] for r in records)),automatic_pool_eligible=sum(r['auto_eligible'] for r in records),
        caption_sources=dict(collections.Counter(r['caption_source'] for r in records)),
        excluded_from_automatic_pool=sum(not r['auto_eligible'] for r in records),
        original_source_kinds=dict(collections.Counter(r['source_kind'] for r in records)),new_image_paths=created,
        all_source_and_staged_bytes_identical=True,all_original_dimensions_preserved=True,
        playlist_files_modified=0,source_bytes_modified=0,console_actions=0)
    (a.audit/'CATALOG_STAGE.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
