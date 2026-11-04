"""Compare scalar/one-worker and AVX2/eight-worker full native source frames."""
from pathlib import Path
import os,subprocess,json,struct,hashlib
root=Path(__file__).resolve().parents[1];records=[]
for build in (1888,6717):
 for frame in (0,15,30):
  files=[]
  for scalar in (True,False):
   file=root/'build'/f'pixel-{build}-{frame}-{scalar}.bmp';env=dict(os.environ,SDL_VIDEODRIVER='dummy')
   if scalar:env['NXE_XUI_SCALAR']='1'
   else:env.pop('NXE_XUI_SCALAR',None)
   result=subprocess.run([str(root/'build/render-xui'),str(root/'private/era-skins/blades/scene'/f'{build}.nxui'),str(root/'private/fonts-owner/Convection.ttf'),str(frame),str(file),'0','1' if scalar else '8'],env=env,cwd=root,capture_output=True,text=True,check=True)
   data=file.read_bytes();offset=struct.unpack_from('<I',data,10)[0];files.append(data[offset:])
  assert len(files[0])==len(files[1])==1920*1080*4
  changed=sum(files[0][i:i+4]!=files[1][i:i+4] for i in range(0,len(files[0]),4))
  records.append(dict(build=build,frame=frame,changed_pixels=changed,scalar_sha256=hashlib.sha256(files[0]).hexdigest(),avx2_sha256=hashlib.sha256(files[1]).hexdigest()))
print(json.dumps(records,indent=2));(root/'build/render-equivalence.json').write_text(json.dumps(records,indent=2)+'\n')
assert not any(r['changed_pixels'] for r in records)
