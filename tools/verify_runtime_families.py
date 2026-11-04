"""Trace source-family host constants and numeric device state from owner binaries."""
from pathlib import Path
import argparse,struct,hashlib,json,collections,re
from audit_original_runtime import Image
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);a=p.parse_args();root=a.root
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
xam=Image(root,'xam');dash=Image(root,'dash');late=Image(root,'dash6717');index=collections.defaultdict(list)
for start,end in late.functions.items():index[signature(late,start,end)].append(start)
matches={}
for name,image,address in [('device_blend',xam,0x81934c18),('begin_element_render',xam,0x81923b80),('viewport_host',dash,0x920a41e0),('animation_step',xam,0x81906788),('glyph_paint',xam,0x8193a168)]:
 candidates=index.get(signature(image,address,image.functions[address]),[]);matches[name]=dict(source=hex(address),late_candidates=[hex(v) for v in candidates])
 for candidate in candidates:(root/(name+'-6717-'+hex(candidate)+'.asm')).write_text(late.dump(candidate),encoding='utf-8')
# Evaluate the actual li/cmp/branch selector, rather than transcribing a table.
instructions={}
for line in xam.dump(0x81934c18).splitlines():
 m=re.match(r'([0-9a-f]+):\s+(\w+)\s+(.*?)(?:\s*;.*)?$',line)
 if m:instructions[int(m[1],16)]=(m[2],m[3].strip())
states=[]
for mode in range(1,7):
 registers={11:mode};pc=0x81934c28;equal=False
 while pc!=0x81934c90:
  op,args=instructions[pc];parts=[s.strip() for s in args.split(',')];next_pc=pc+4
  if op=='li':registers[int(parts[0][1:])]=int(parts[1],0)
  elif op=='cmpwi':equal=registers[int(parts[-2][1:])]==int(parts[-1],0)
  elif op=='b' or (op=='beq' and equal) or (op=='bne' and not equal):next_pc=int(parts[-1],0)
  elif op=='lwz':pass
  else:assert op in ('beq','bne'),(op,args)
  pc=next_pc
 states.append(dict(mode=mode,operation=registers[4],source=registers[30],destination=registers[31]))
expected=[(0,6,7),(0,0,4),(2,1,1),(3,1,1),(0,6,1),(1,6,1)]
assert [tuple(s[k] for k in ('operation','source','destination')) for s in states]==expected
constants={}
for image,address,name,expected_value in [(xam,0x81873e84,'frame_ms',1000/60),(xam,0x81876c90,'font_dpi',96),(xam,0x81876e08,'one_over_72',1/72),(dash,0x92000c68,'viewport_x',8/7),(dash,0x92000c64,'viewport_y',12/11),(dash,0x92000c60,'viewport_translation_y',-64)]:
 value=struct.unpack_from('>f',image.data,address-image.base)[0];assert abs(value-expected_value)<.00001
 constants[name]=dict(address=hex(address),value=value)
report=dict(images={image.name:dict(base=hex(image.base),sha256=hashlib.sha256(image.data).hexdigest()) for image in [xam,dash,late]},blend_states=states,constants=constants,family_matches=matches,
 group_compositing=dict(element_begin='0x81923b80',children='0x81923d40',mode_zero_inherits=True,blend_does_not_create_offscreen_layer=True),
 text_style=dict(shadow_bit=1,shadow_shader_function='0x8193a168',point_to_scene='96 / 72',font_weight='actual face resource; bit 0 is not bold'))
(root/'verified-runtime.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(matches,indent=2));print('blend selector and constants verified')
