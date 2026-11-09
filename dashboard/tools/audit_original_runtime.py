"""Read owner-supplied XEX runtime functions statically; never execute Xbox code."""
from pathlib import Path
import argparse,bisect,hashlib,json,re,struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'audit-deps'))
import capstone,pefile

class Image:
 def __init__(self,root,name):
  self.root=root;self.name=name;raw=(root/(name+'.xex')).read_bytes();self.data=(root/(name+'.pe')).read_bytes();self.pe=pefile.PE(data=self.data)
  self.u=lambda at:struct.unpack_from('>I',self.data,at)[0]
  sec=struct.unpack_from('>I',raw,16)[0];self.base=struct.unpack_from('>I',raw,sec+0x110)[0]
  self.functions={}
  pdata=next(s for s in self.pe.sections if s.Name.rstrip(b'\0')==b'.pdata')
  for at in range(pdata.VirtualAddress,pdata.VirtualAddress+pdata.Misc_VirtualSize,8):
   start=self.u(at);packed=self.u(at+4);length=((packed>>8)&0x3fffff)*4
   if self.base<=start<self.base+len(self.data) and length:self.functions[start]=start+length
  self.starts=sorted(self.functions);self.exports={};self.imports={};self.calls={}
  names={int(o,16):n for o,n in re.findall(r'XE_EXPORT\(xam,\s+(0x[0-9A-Fa-f]+),\s+(\w+)',(root/'xam_table.inc').read_text())}
  export=struct.unpack_from('>I',raw,sec+0x160)[0]
  if export:
   table=export-self.base;n=self.u(table+0x24);first=self.u(table+0x28)
   for i in range(n):
    offset=self.u(table+0x2c+i*4)
    if offset:self.exports[names.get(first+i,'ordinal_'+str(first+i))]=self.base+offset
  idc=root/(name+'.idc')
  if idc.exists():
   for word,stub,ordinal,lib in re.findall(r'SetupImportFunc\((0x[0-9A-F]+), (0x[0-9A-F]+), (0x[0-9A-F]+), "([^"]+)"\)',idc.read_text()):
    self.imports[int(stub,16)]=names.get(int(ordinal,16),lib+'_'+ordinal) if lib.lower()=='xam.xex' else lib+'_'+ordinal
  text=next(s for s in self.pe.sections if s.Name.rstrip(b'\0')==b'.text')
  for offset in range(text.VirtualAddress,text.VirtualAddress+text.Misc_VirtualSize,4):
   at=self.base+offset;word=self.u(offset)
   if word>>26!=18:continue
   displacement=word&0x3fffffc
   if displacement&0x2000000:displacement-=0x4000000
   target=(displacement if word&2 else at+displacement)&0xffffffff
   owner=self.containing(at)
   if word&1 or owner is None or target<owner or target>=self.functions[owner]:self.calls.setdefault(target,[]).append(at)
  self.cs=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_32|capstone.CS_MODE_BIG_ENDIAN);self.cs.skipdata=True
 def containing(self,address):
  i=bisect.bisect_right(self.starts,address)-1
  return self.starts[i] if i>=0 and address<self.functions[self.starts[i]] else None
 def dump(self,address):
  start=self.containing(address) or address;end=self.functions.get(start,start+512);register={};lines=[]
  if start not in self.functions:
   for at in range(start,min(end,self.base+len(self.data)-4),4):
    word=self.u(at-self.base)
    if word==0x4e800020 or (word>>26==18 and not word&1):end=at+4;break
  reverse={v:k for k,v in self.exports.items()}
  for ins in self.cs.disasm(self.data[start-self.base:end-self.base],start):
   comment='';parts=ins.op_str.split(', ')
   try:
    if ins.mnemonic=='lis':register[parts[0]]=(int(parts[1],0)<<16)&0xffffffff
    elif ins.mnemonic=='li':register[parts[0]]=int(parts[1],0)&0xffffffff
    elif ins.mnemonic in ('addi','ori'):
     value=register.get(parts[1]);immediate=int(parts[2],0)
     if value is not None:
      value=(value+immediate if ins.mnemonic=='addi' else value|immediate)&0xffffffff;register[parts[0]]=value
      if self.base<=value<self.base+len(self.data):comment=f'address {value:08x}'
    elif ins.mnemonic in ('lfs','lfd'):
     m=re.fullmatch(r'(-?0x[0-9a-f]+|-?\d+)\((r\d+)\)',parts[1]);value=register.get(m[2]) if m else None
     if value is not None:
      offset=value+int(m[1],0)-self.base
      if 0<=offset<len(self.data)-8:comment='constant '+str(struct.unpack_from('>f' if ins.mnemonic=='lfs' else '>d',self.data,offset)[0])
    elif ins.mnemonic in ('b','bl'):
     target=int(parts[-1],0);comment=self.imports.get(target,reverse.get(target,''))
     if ins.mnemonic=='bl':
      for r in ['r0','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12']:register.pop(r,None)
    elif parts and parts[0] in register:register.pop(parts[0],None)
   except (ValueError,IndexError,struct.error):pass
   lines.append(f'{ins.address:08x}: {ins.mnemonic:12} {ins.op_str:38}'+(' ; '+comment if comment else ''))
  return '\n'.join(lines)+'\n'
 def save(self):
  report=dict(base=self.base,image_sha256=hashlib.sha256(self.data).hexdigest(),functions=len(self.functions),exports=self.exports,imports={hex(k):v for k,v in self.imports.items()},calls={hex(k):[hex(v) for v in values] for k,values in self.calls.items()})
  (self.root/(self.name+'-static.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--image',default='xam');p.add_argument('--address',type=lambda x:int(x,0));a=p.parse_args();image=Image(a.root,a.image);image.save()
 if a.address:print(image.dump(a.address))
 else:
  for name,address in image.exports.items():
   if name.startswith('Xui') and any(v in name for v in ['Anim','Timeline','Font','Render']):
    (a.root/(name+'.asm')).write_text(image.dump(address),encoding='utf-8');print(name,hex(address))
  if a.image=='dash':
   for address,name in image.imports.items():
    if name in ['XuiRenderSetViewTransform','XuiRenderInit','XuiRenderInitShared','XuiAnimRun']:
     for caller in image.calls.get(address,[]):
      (a.root/(name+'-caller-'+hex(caller)+'.asm')).write_text(image.dump(caller),encoding='utf-8');print(name,'caller',hex(caller))
