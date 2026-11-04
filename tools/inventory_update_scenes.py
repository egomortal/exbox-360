"""Extract and inventory authored scene/timeline data for every cached update."""
from pathlib import Path,PurePosixPath
import argparse,collections,concurrent.futures,hashlib,json,os,posixpath,struct,subprocess,xml.etree.ElementTree as ET
from scene_catalog import active_bundle,inventory_path,decoder_settings,protect_active_output
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--decode-only',action='store_true');p.add_argument('--extractor',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();assets=a.assets.resolve();archive=assets/'360dasharchive'
if active_bundle(assets) and not a.output:p.error('Use --output for a new recovery; the active bundle is immutable.')
out=(a.output or assets/'scene-audit/revisions').resolve()
if not out.is_relative_to(assets):p.error('--output must stay inside the asset root.')
if active_bundle(assets) and out.exists():p.error('Use a fresh output directory for a versioned recovery.')
protect_active_output(assets,out)
out.mkdir(parents=True,exist_ok=True)
packages={'dashmain','dashuisk','dashskn1','dashskn2','controlp','SharedUI','homepage','hubui','epix','dashcomm'}
settings=decoder_settings(assets);cli=assets/settings['cli'];schema=assets/settings['schema'];dotnet=assets/'tools/dotnet-linux/dotnet'
tool=a.extractor or assets/'tools/xextool-re-recovery/xextool-re.exe'
env=dict(os.environ,DOTNET_ROOT=str(dotnet.parent),DOTNET_ROLL_FORWARD='Major')
def sha(data):return hashlib.sha256(data).hexdigest()
def unpack(package,destination):
 data=package.read_bytes()
 if data[:4]!=b'XUIZ':return []
 version=struct.unpack_from('>I',data,4)[0];count=struct.unpack_from('>H',data,20)[0];at=22;records=[]
 for _ in range(count):
  size,offset,length=struct.unpack_from('>IIB',data,at);at+=9;length*=2 if version==1 else 1
  name=data[at:at+length].decode('utf-16be' if version==1 else 'latin1').replace('\\','/');at+=length
  normalized=posixpath.normpath(destination.name+'/'+name)
  if name.startswith('/') or ':' in name or normalized=='..' or normalized.startswith('../'):raise ValueError('Unsafe resource path')
  parts=PurePosixPath(normalized).parts
  if any('-' in part and len(part)==5 and part.lower()!='en-us' for part in parts):continue
  suffix=Path(name).suffix.lower()
  if suffix not in {'.xur','.xus','.wav','.xma','.wma','.mp3','.snd'}:continue
  target=destination.parent/normalized;records.append((target,offset,size))
 files=[]
 for target,offset,size in records:
  payload=data[at+offset:at+offset+size]
  if len(payload)!=size:raise ValueError('Resource bounds')
  target.parent.mkdir(parents=True,exist_ok=True)
  if not target.exists() or sha(target.read_bytes())!=sha(payload):target.write_bytes(payload)
  files.append(target)
 return files
if a.decode_only:
 previous=json.loads(inventory_path(assets).read_text());versions=previous['updates'];errors=previous['errors'];unique={}
 for item in versions:
  for variant in item['variants']:
   for scene in variant['scenes']:unique.setdefault(scene['sha256'],archive/scene['path'])
else:
 updates=json.loads((archive/'update-catalog.json').read_text())['updates'];versions=[];unique={};errors=[]
 for item in updates:
  variants=[]
  for source in item['paths']:
   relative=Path(source).parent;resource=archive/'resources'/relative;unpacked=archive/'unpacked'/relative
   if not (archive/'files'/source).is_file():
    variants.append(dict(source=source,scenes=[],sounds=[],extraction_error='Source is a supplied package, not an extracted dashboard XEX'));continue
   if not resource.is_dir() or not any(resource.iterdir()):
    resource.mkdir(parents=True,exist_ok=True);run=subprocess.run([str(tool),'resources',str(archive/'files'/source),str(resource)],capture_output=True,text=True)
    if run.returncode:errors.append(dict(path=source,error=run.stderr+run.stdout));continue
   scenes=[];sounds=[]
   for package in sorted(resource.iterdir()):
    if not package.is_file() or package.name not in packages:continue
    try:
     for file in unpack(package,unpacked/package.name):
      payload=file.read_bytes();identity=sha(payload)
      if file.suffix.lower()=='.xur':
       unique.setdefault(identity,file);scenes.append(dict(path=str(file.relative_to(archive)),sha256=identity,package=package.name))
      elif file.suffix.lower() in {'.wav','.xma','.wma','.mp3','.snd'}:sounds.append(dict(path=str(file.relative_to(archive)),sha256=identity))
    except Exception as exc:errors.append(dict(path=str(package),error=str(exc)))
   variants.append(dict(source=source,scenes=scenes,sounds=sounds))
  versions.append(dict(family=item['family'],build=item['build'],preview=item['preview'],variants=variants))
if not a.decode_only and (assets/'owner-12413/recovered-content.json').exists():
 owner=json.loads((assets/'owner-12413/recovered-content.json').read_text())
 update=next(u for u in versions if u['build']==12413)
 update['source_scope']='companion_modules_only_main_dashboard_absent'
 for module in owner['modules']:
  scenes=[];sounds=[]
  for record in module['resources']:
   package=assets/record['path']
   if package.read_bytes()[:4]!=b'XUIZ':continue
   destination=archive/'unpacked/Kinect/Beta/12413-owner-modules'/Path(module['source']).name/package.name
   try:
    for file in unpack(package,destination):
     identity=sha(file.read_bytes())
     if file.suffix.lower()=='.xur':
      unique.setdefault(identity,file);scenes.append(dict(path=str(file.relative_to(archive)),sha256=identity,package=package.name))
     elif file.suffix.lower() in {'.wav','.xma','.wma','.mp3','.snd'}:sounds.append(dict(path=str(file.relative_to(archive)),sha256=identity))
   except Exception as exc:errors.append(dict(path=str(package),error=str(exc)))
  update['variants'].append(dict(source=module['source'],scope='supplied_companion_module',scenes=scenes,sounds=sounds))
print(f'{len(versions)} updates, {sum(len(v["scenes"]) for u in versions for v in u["variants"])} scene references, {len(unique)} unique scenes',flush=True)

def decode(entry):
 identity,source=entry;xml=out/'decoded'/(identity+'.xui');log=out/'decoded'/(identity+'.log');xml.parent.mkdir(exist_ok=True)
 version=struct.unpack_from('>I',source.read_bytes(),4)[0]
 if version==7:
  # Successful old XML must not hide changes to the strict decoder.
  parser_hash=sha(Path(__file__).with_name('decode_xur7.py').read_bytes())
  if settings.get('strict_v7_decoder_sha256',parser_hash)!=parser_hash:raise ValueError('Strict V7 decoder changed; verify a new decoder bundle before refreshing scenes.')
  proof_file=xml.with_suffix('.verification.json')
  cached=json.loads(proof_file.read_text()) if proof_file.exists() else {}
  if cached.get('decoder_sha256')!=parser_hash:
   try:
    from decode_xur7 import decode as decode7
    pending=xml.with_suffix('.pending')
    proof=decode7(source,schema,pending)
    proof.update(decoder_sha256=parser_hash,source_sha256=identity,curve_semantics_verified=False)
    pending.replace(xml);proof_file.write_text(json.dumps(proof,indent=2)+'\n')
   except Exception as exc:return identity,dict(error='XUR7: '+str(exc),stale_xml_not_used=xml.exists())
 if not xml.exists():
  if version==7:
   try:
    from decode_xur7 import decode as decode7
    proof=decode7(source,schema,xml)
    xml.with_suffix('.verification.json').write_text(json.dumps(proof,indent=2)+'\n')
   except Exception as exc:return identity,dict(error='XUR7: '+str(exc))
  elif version in (5,8):
   run=subprocess.run([str(dotnet),str(cli),'conv','-s',str(source),'-f','xuiv12','-o',str(xml),'-g','V'+str(version),'-i','-l',str(log),'-v','info'],capture_output=True,text=True,env=env)
   if run.returncode or not xml.exists():return identity,dict(error=run.stdout+run.stderr,log=str(log))
  else:return identity,dict(error='Unsupported XUR version '+str(version))
 try:
  root=ET.parse(xml).getroot();scenes=[]
  for node in root.iter():
   timelines=node.find('Timelines')
   if timelines is None:continue
   track=timelines.findall('Timeline');keys=[k for t in track for k in t.findall('KeyFrame')]
   markers=[dict(name=n.findtext('Name') or '',frame=float(n.findtext('Time') or 0),command=n.findtext('Command') or '',target=n.findtext('Parameter') or n.findtext('Target') or '') for n in timelines.findall('NamedFrames/NamedFrame')]
   scenes.append(dict(id=node.findtext('Properties/Id') or '',kind=node.tag,tracks=len(track),keys=len(keys),last=max([float(k.findtext('Time')) for k in keys]+[m['frame'] for m in markers]+[0]),markers=markers))
  roots=[dict(kind=n.tag,id=n.findtext('Properties/Id') or '',width=n.findtext('Properties/Width'),height=n.findtext('Properties/Height'),host_class=n.findtext('Properties/ClassOverride')) for n in root if n.tag.startswith('Xui')]
  return identity,dict(xml=str(xml.relative_to(assets)),xml_sha256=sha(xml.read_bytes()),roots=roots,timelines=scenes,tracks=sum(n['tracks'] for n in scenes),keys=sum(n['keys'] for n in scenes))
 except Exception as exc:return identity,dict(error=str(exc))
first_identity,first_result=decode(next(iter(unique.items())))
if 'error' in first_result:raise RuntimeError(first_result)
results={}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for index,(identity,result) in enumerate(pool.map(decode,unique.items()),1):
  results[identity]=result
  if index%25==0:print(f'decoded {index}/{len(unique)}',flush=True)
report=dict(source_commit=json.loads((archive/'update-catalog.json').read_text())['commit'],updates=versions,scenes=results,errors=errors,decoder=settings)
(out/'inventory.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
summary=dict(updates=len(versions),scene_references=sum(len(v['scenes']) for u in versions for v in u['variants']),unique_scenes=len(unique),decoded=sum('error' not in r for r in results.values()),failed=[dict(sha256=k,**r) for k,r in results.items() if 'error' in r],extraction_errors=errors)
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:(len(v) if k in ('failed','extraction_errors') else v) for k,v in summary.items()}),flush=True)
