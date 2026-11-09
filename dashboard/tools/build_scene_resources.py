"""Resolve scene URI keys against their own update's extracted owner resources."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,shutil
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--scenes',type=Path,required=True);a=p.parse_args();output=a.scenes/'resources';output.mkdir(exist_ok=True);objects=output/'objects';objects.mkdir(exist_ok=True);reports=[]
for revision in json.loads((a.scenes/'revisions.json').read_text()):
 build=revision['build'];data=(a.scenes/revision['pack']).read_bytes();string_size=struct.unpack_from('<8I',data,8)[7]
 refs=sorted({s.decode('utf-8') for s in data[-string_size:].split(b'\0') if s.lower().endswith((b'.png',b'.jpg',b'.dds',b'.xma',b'.wav'))})
 source=a.assets/'360dasharchive/unpacked/Blades/Retail'/str(build)
 # shrdres is itself an XUIZ package, despite its .xzp extension. Earlier
 # image extraction omitted some PCM/XMA entries; resolve this exact revision.
 package=a.assets/'360dasharchive/files/Blades/Retail'/str(build)/'shrdres.xzp'
 if package.exists():
  raw=package.read_bytes();assert raw[:4]==b'XUIZ';version=struct.unpack_from('>I',raw,4)[0];count=struct.unpack_from('>H',raw,20)[0];at=22;entries=[]
  needed={r.replace('\\','/').split('/')[-1].lower() for r in refs if r.startswith('sharedres://')}
  for _ in range(count):
   size,offset,n=struct.unpack_from('>IIB',raw,at);at+=9;n*=2 if version==1 else 1
   name=raw[at:at+n].decode('utf-16be' if version==1 else 'latin1').replace('\\','/');at+=n
   if name.split('/')[-1].lower() in needed:entries.append((name.split('/')[-1],offset,size))
  for name,offset,size in entries:
   payload=raw[at+offset:at+offset+size];assert len(payload)==size
   target=source/'shared'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(payload)
 files=[f for f in source.rglob('*') if f.is_file()];mapping=[];resolved=[];missing=[]
 for ref in refs:
  name=ref.replace('\\','/').split('/')[-1].lower();candidates=[f for f in files if f.name.lower()==name]
  preferred=['shared'] if ref.startswith('sharedres://') else ['dashcomm'] if ref.startswith('common://') else ['dashmain','dashuisk']
  candidates.sort(key=lambda f:(preferred.index(f.relative_to(source).parts[0]) if f.relative_to(source).parts[0] in preferred else 99,len(f.parts),str(f)))
  if not candidates:missing.append(ref);continue
  file=candidates[0];raw=file.read_bytes();digest=hashlib.sha256(raw).hexdigest();sound=file.suffix.lower() in ('.xma','.wav');dest=objects/(digest+('.wav' if sound else file.suffix.lower()))
  if not dest.exists():
   if sound:
    run=subprocess.run(['ffmpeg','-v','error','-threads','1','-i',str(file),'-ac','2','-ar','48000','-c:a','pcm_s16le','-fflags','+bitexact','-flags:a','+bitexact','-y',str(dest)],capture_output=True,text=True)
    if run.returncode:missing.append(dict(key=ref,error=run.stderr));continue
   else:shutil.copyfile(file,dest)
  mapping.append(ref+'\t'+str(dest.relative_to(output)).replace('\\','/'))
  resolved.append(dict(key=ref,source=str(file.relative_to(a.assets)),source_sha256=digest,file=str(dest.relative_to(output)),sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),audio=sound))
 (output/f'{build}.tsv').write_text('\n'.join(mapping)+'\n',encoding='utf-8')
 reports.append(dict(build=build,resolved=resolved,missing=missing))
(output/'manifest.json').write_text(json.dumps(reports,indent=2)+'\n',encoding='utf-8')
print(json.dumps([dict(build=r['build'],resolved=len(r['resolved']),missing=r['missing']) for r in reports],indent=2))
