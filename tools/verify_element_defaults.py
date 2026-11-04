"""Verify omitted element dimensions using original early/late Blades code."""
from pathlib import Path
import argparse,collections,hashlib,json,struct
from audit_original_runtime import Image

def signature(image,start,end):
 words=struct.unpack('>'+str((end-start)//4)+'I',image.data[start-image.base:end-image.base]);address_regs=set();result=[]
 for word in words:
  op=word>>26;rt=(word>>21)&31;ra=(word>>16)&31;masked=word
  if op==18:masked=word&0xfc000003;address_regs.difference_update(range(13))
  elif op==16:masked=word&0xffff0003
  elif op==15 and ra==0:address_regs.add(rt);masked=word&0xffff0000
  elif op in [14,24,32,34,40,42,48,50,36,38,44,52,54] and ra in address_regs:
   masked=word&0xffff0000
   if op not in [14,24,36,38,44,52,54]:address_regs.discard(rt)
  elif op in [14,32,34,40,42]:address_regs.discard(rt)
  result.append(masked)
 return hashlib.sha256(struct.pack('>'+str(len(result))+'I',*result)).hexdigest()

def constant_users(image,value):
 text=next(s for s in image.pe.sections if s.Name.rstrip(b'\0')==b'.text')
 needle=struct.pack('>f',value);constants=[];at=0
 while True:
  at=image.data.find(needle,at)
  if at<0:break
  if at%4==0:constants.append(image.base+at)
  at+=4
 users=set()
 for at in range(text.VirtualAddress,text.VirtualAddress+text.Misc_VirtualSize,4):
  word=image.u(at)
  if word>>26!=48:continue
  for address in constants:
   if word&0xffff!=address&0xffff:continue
   ra=(word>>16)&31;lis=(15<<26)|(ra<<21)|(((address+0x8000)>>16)&0xffff)
   if any(image.u(p)==lis for p in range(max(text.VirtualAddress,at-160),at,4)):
    owner=image.containing(image.base+at)
    if owner is not None and image.functions[owner]-owner<=256:users.add(owner)
 return users

def main():
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(exist_ok=True)
 early=Image(a.root,'xam');late=Image(a.root,'dash6717')
 base_signature=signature(early,0x81924230,early.functions[0x81924230])
 candidates=[]
 for start,end in late.functions.items():
  if signature(late,start,end)!=base_signature:continue
  text=late.dump(start)
  for value in (60,30):
   if 'constant '+str(value)+'.0' in text:
    candidates.append((start,value));(a.output/('default-'+str(value)+'-6717-'+hex(start)+'.asm')).write_text(text)
 addresses={value:[address for address,v in candidates if v==value] for value in (60,30)}
 if not addresses[60] or not addresses[30]:
  addresses={value:sorted(constant_users(late,value)) for value in (60,30)}
  for value,functions in addresses.items():
   for start in functions:(a.output/('default-candidate-'+str(value)+'-'+hex(start)+'.asm')).write_text(late.dump(start))
 owners=[]
 pairs=[]
 for width in addresses[60]:
  for height in addresses[30]:
   for caller in late.calls.get(width,[]):
    owner=late.containing(caller);text=late.dump(caller)
    if hex(height) in text and '0xc(r31)' in text and '0x10(r31)' in text:
     owners.append(owner);pairs.append((width,height));(a.output/('element-constructor-6717-'+hex(owner)+'.asm')).write_text(text)
 if not owners:
  (a.output/'late-default-candidates.json').write_text(json.dumps({str(k):[hex(v) for v in a] for k,a in addresses.items()},indent=2))
 assert owners,'No late constructor using both verified dimensions'
 assert len(set(pairs))==1,pairs
 width,height=pairs[0]
 assert struct.unpack_from('>f',early.data,0x81875008-early.base)[0]==60
 assert struct.unpack_from('>f',early.data,0x818751e4-early.base)[0]==30
 report=dict(width=60,height=30,early=dict(constructor='0x81926710',width_default='0x81924230',height_default='0x819242a0',width_constant='0x81875008',height_constant='0x818751e4'),late=dict(width_default=hex(width),height_default=hex(height),constructors=[hex(v) for v in owners]),image_sha256={image.name:hashlib.sha256(image.data).hexdigest() for image in (early,late)},method='Original constructor stores default-property floats at element offsets 0xc and 0x10; constant functions matched independently in 6717')
 (a.output/'element-defaults.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
