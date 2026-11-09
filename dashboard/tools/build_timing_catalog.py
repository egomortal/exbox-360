"""Save revision-specific timing coverage, ranges, raw evidence and decoder gaps."""
from pathlib import Path
import argparse,json,struct,hashlib
from read_xur8_timing import read as read8
from scene_catalog import active_bundle,inventory_path,protect_active_output
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--inventory',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
if active_bundle(a.assets) and not a.output:p.error('Use --output for a new timing audit; the active bundle is immutable.')
out=a.output or a.assets/'scene-audit/revisions';protect_active_output(a.assets,out);out.mkdir(parents=True,exist_ok=True);inventory=json.loads(inventory_path(a.assets,a.inventory).read_text());sources={};updates=[]
for update in inventory['updates']:
 refs=[s for v in update['variants'] for s in v['scenes']];ids=sorted({s['sha256'] for s in refs})
 for source in refs:
  identity=source['sha256']
  if identity in sources:continue
  original=a.assets/'360dasharchive'/source['path'];data=original.read_bytes();assert hashlib.sha256(data).hexdigest()==identity
  item=inventory['scenes'][identity];record=dict(original=source['path'],version=struct.unpack_from('>I',data,4)[0],source_sha256=identity)
  if 'error' not in item:
   record.update(timelines=item['timelines'],tracks=item['tracks'],keys=item['keys'],xml=item['xml'],xml_sha256=item['xml_sha256'])
   for t in record['timelines']:
    names={m['name']:m for m in t['markers']};ranges=[]
    for name,m in names.items():
     candidates=[name+'End','End'+name]
     if name.endswith('Start'):candidates.append(name[:-5]+'End')
     if name.endswith('Begin'):candidates.append(name[:-5]+'End')
     for end_name in candidates:
      if end_name in names and names[end_name]['frame']>=m['frame']:
       end=names[end_name]['frame'];ranges.append(dict(name=name,end_name=end_name,start=m['frame'],end=end,frames=end-m['frame'],milliseconds_at_60fps=(end-m['frame'])*1000/60));break
    t['ranges']=ranges
  else:record['decode_error']=item['error']
  if record['version']==8:
   try:record['raw_tables']=read8(data)
   except Exception as exc:record['raw_table_error']=str(exc)
  sources[identity]=record
 timed=[s for s in ids if sources[s].get('timelines') or sources[s].get('raw_tables',{}).get('keys') or sources[s].get('raw_tables',{}).get('markers')]
  # Every catalog update must have its own source timing, including identical hashes.
 updates.append(dict(family=update['family'],build=update['build'],preview=update['preview'],source_scope=update.get('source_scope','dashboard_module_resources'),scenes=ids,timed_scenes=timed,decoded=sum('decode_error' not in sources[s] for s in ids),unresolved=[s for s in ids if 'decode_error' in sources[s]]))
assert len(updates)==97, len(updates)
# Incomplete source revisions are saved as gaps, not silently aliased or dropped.
for update in updates:
 update['coverage']='partial' if update['timed_scenes'] else 'missing_source_timing'
report=dict(source_commit=inventory['source_commit'],updates=updates,scenes=sources,frame_rate_evidence='1888 XAM 0x81906788 and 6717 dash 0x92154e10; later-era millisecond columns are explicitly conversions at 60 fps',
 notes=['Original key positions and named stops are retained per revision. Importing timing does not emulate host callbacks.','V7 extended easing values and V8 custom key flags remain explicit; these are not silently promoted to supported runtime curves.'])
(out/'timing-catalog.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
summary=dict(updates=len(updates),unique_scenes=len(sources),decoded=sum('decode_error' not in s for s in sources.values()),unresolved=sum('decode_error' in s for s in sources.values()),raw_v8_verified=sum('raw_tables' in s for s in sources.values()),raw_v8_errors=sum('raw_table_error' in s for s in sources.values()),updates_with_timing=sum(bool(u['timed_scenes']) for u in updates),gaps=[dict(family=u['family'],build=u['build'],coverage=u['coverage']) for u in updates if not u['timed_scenes']])
(out/'timing-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
