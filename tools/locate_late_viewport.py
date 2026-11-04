from pathlib import Path
import struct,json
from audit_original_runtime import Image
root=Path('C:/Users/USER/Downloads/jailbreak stuff/asset/exbox360/runtime-verification-20261025');im=Image(root,'dash6717');text=next(s for s in im.pe.sections if s.Name.rstrip(b'\0')==b'.text');report={}
for value in [8/7,12/11,-64,96,1000/60]:
 needle=struct.pack('>f',value);offsets=[];pos=0
 while True:
  pos=im.data.find(needle,pos)
  if pos<0:break
  if pos%4==0:offsets.append(pos)
  pos+=4
 users=[]
 for offset in offsets:
  address=im.base+offset;low=address&0xffff;high=((address+0x8000)>>16)&0xffff
  for at in range(text.VirtualAddress,text.VirtualAddress+text.Misc_VirtualSize,4):
   word=im.u(at)
   if word>>26!=48 or word&0xffff!=low:continue
   ra=(word>>16)&31;lis=(15<<26)|(ra<<21)|high
   if any(im.u(p)==lis for p in range(max(text.VirtualAddress,at-160),at,4)):
    fn=im.containing(im.base+at)
    if fn is not None:
     users.append(dict(constant=hex(address),instruction=hex(im.base+at),function=hex(fn)));(root/(f'late-constant-{value:.4g}-{hex(fn)}.asm')).write_text(im.dump(fn),encoding='utf-8')
 report[str(value)]=users
(root/'late-constant-users.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
