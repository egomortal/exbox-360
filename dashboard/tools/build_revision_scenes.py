"""Compile an individually traced Blades pack for every catalogued update."""
from pathlib import Path
import argparse,json,hashlib,contextlib,io
from compile_xui_scene import Compiler
from scene_catalog import inventory_path,protect_active_output
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--inventory',type=Path,help='Versioned scene catalog; defaults to the active recovered catalog.');a=p.parse_args()
protect_active_output(a.assets,a.output)
inventory=json.loads(inventory_path(a.assets,a.inventory).read_text());a.output.mkdir(parents=True,exist_ok=True);records=[]
for update in inventory['updates']:
 if update['family']!=0:continue
 build=update['build'];variant=update['variants'][0];source={s['path'].replace('\\','/').split('/')[-2]+'/'+Path(s['path']).name:s for s in variant['scenes']}
 def pair(key):
  info=source[key];decoded=inventory['scenes'][info['sha256']];xml=a.assets/decoded['xml']
  if hashlib.sha256(xml.read_bytes()).hexdigest()!=decoded['xml_sha256']:raise ValueError('Decoded scene hash mismatch: '+str(xml))
  return xml,a.assets/'360dasharchive'/info['path']
 try:
  main=pair('dashmain/dashmain.xur');skin=pair('dashskn1/BladeSkin.xur');controls=pair('dashuisk/skin.xur')
  compiler=Compiler([skin,controls,main]).compile(main[0])
  if compiler.missing or compiler.unhandled:raise ValueError((sorted(compiler.missing),sorted(compiler.unhandled)))
  with contextlib.redirect_stdout(io.StringIO()):compiler.write(a.output/f'{build}.nxui')
  tabs=[n['id'] for n in compiler.nodes if n['id'].startswith('Tab') and n['id'][3:].isdigit()]
  records.append(dict(build=build,pack=f'{build}.nxui',sha256=hashlib.sha256((a.output/f'{build}.nxui').read_bytes()).hexdigest(),nodes=len(compiler.nodes),keys=len(compiler.keys),tracks=len(compiler.tracks),tabs=tabs,sources=[s['xur_sha256'] for s in compiler.source]))
 except Exception as exc:records.append(dict(build=build,error=str(exc)))
(a.output/'revisions.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
print(json.dumps(records,indent=2))
assert all('error' not in r for r in records)
