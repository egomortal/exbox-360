"""Strict decoder for the inline, packed XUR7 format used by early Kinect betas.

Original bytes remain unchanged. Every section reference and final DATA cursor
is checked; an undecoded scene is reported as a failure, never accepted as V8.
"""
from pathlib import Path
import argparse,struct,xml.etree.ElementTree as E,json
class Decoder:
 def __init__(self,file,schema,ignore=True):
  self.data=file.read_bytes();self.at=0;self.classes={};self.parts={};self.nodes=0;self.key_count=0;self.timeline_count=0;self.marker_count=0
  assert self.data[:4]==b'XUIB' and self.u32(4)==7,'Not XUR7'
  assert self.u32(14)==len(self.data),'File length mismatch'
  count=self.u16(18);at=60 if self.u32(8) else 20
  self.counts=struct.unpack_from('>10I',self.data,20) if at==60 else None
  for i in range(count):
   name,off,size=struct.unpack_from('>4sII',self.data,at+i*12);assert off+size<=len(self.data)
   self.parts[name.decode()]=(off,size)
  for f in sorted(schema.glob('*.xml')):
   for c in E.parse(f).getroot().findall('XUIClass'):self.classes[c.get('Name')]=(c.get('BaseClassName'),list(c.findall('PropDef')))
  for c in E.parse(schema/'17559.xhe').findall('.//IgnoreClass') if ignore else []:
   name=c.get('ClassName');ignored={v.text for v in c}
   if name in self.classes:self.classes[name]=(self.classes[name][0],[v for v in self.classes[name][1] if v.get('Name') not in ignored])
  off,size=self.parts['STRN'];byte_length=self.u32(off);n=self.u16(off+4)
  assert byte_length+6==size
  self.strings=['']+self.data[off+6:off+size].decode('utf-8').rstrip('\0').split('\0');assert len(self.strings)==n+1
 def u16(self,at):return struct.unpack_from('>H',self.data,at)[0]
 def u32(self,at):return struct.unpack_from('>I',self.data,at)[0]
 def byte(self):
  v=self.data[self.at];self.at+=1;return v
 def packed(self):
  v=self.byte()
  if v<240:return v
  if v<255:return ((v&15)<<8)|self.byte()
  v=self.u32(self.at);self.at+=4;return v
 def string(self,index):return self.strings[index]
 def table(self,name,index,fmt,width):
  off,size=self.parts[name];assert index>=0 and (index+1)*width<=size,(name,index,size,self.at)
  vals=struct.unpack_from(fmt,self.data,off+index*width)
  return ','.join(str(v) if isinstance(v,int) else format(v,'.9g') for v in vals)
 def hierarchy(self,kind):
  assert kind in self.classes,'Missing class '+kind
  base,props=self.classes[kind]
  return ([] if base in (None,'(null)','') else self.hierarchy(base))+[(kind,props)]
 def scalar(self,p):
  kind=p.get('Type');v=self.packed()
  if kind in ('string','stringtable'):return self.string(v)
  if kind=='bool':return 'true' if v else 'false'
  if kind in ('integer','unsigned'):return str(v if kind=='unsigned' or v<0x80000000 else v-0x100000000)
  if kind=='float':return self.table('FLOT',v,'>f',4)
  if kind=='vector':return self.table('VECT',v,'>3f',12)
  if kind=='quaternion':return self.table('QUAT',v,'>4f',16)
  if kind in ('colour','color'):return '0x'+format(int(self.table('COLR',v,'>I',4)),'08x')
  if kind=='custom':return 'CUST:'+str(v)
  raise ValueError(('Unknown type',kind,p.attrib,self.at))
 def property(self,p,indexed=True):
  if p.get('Type')=='object':
   name={'Fill':'XuiFigureFill','Gradient':'XuiFigureFillGradient','Stroke':'XuiFigureStroke'}[p.get('Name')]
   return self.properties(name)
  n=self.packed() if indexed and 'indexed' in (p.get('Flags') or '').lower() else 1
  return [self.scalar(p) for _ in range(n)]
 def properties(self,kind):
  count=self.packed();result=[];actual=0
  for _,props in self.hierarchy(kind):
   mask=self.packed();assert mask>>len(props)==0,('Mask overflow',kind,mask,len(props),self.at)
   for index,p in enumerate(props):
    if mask&(1<<index):
     vals=self.property(p);result.append((p.get('Name'),vals));actual+=len(vals) if 'indexed' in (p.get('Flags') or '').lower() else 1
  assert actual==count,('Property count',kind,actual,count,self.at)
  return result
 def write_props(self,node,items):
  box=E.SubElement(node,'Properties')
  for name,values in items:
   if values and isinstance(values[0],tuple):self.write_props(E.SubElement(box,name),values)
   else:
    for value in values:E.SubElement(box,name).text=value
 def object(self):
  start=self.at;kind=self.string(self.packed());flags=self.byte();assert flags<16,(kind,flags,start)
  self.hierarchy(kind);node=E.Element(kind);self.nodes+=1
  if flags&1:self.write_props(node,self.properties(kind))
  assert not flags&8,'XUR7 shared objects require separate verification'
  if flags&2:
   for _ in range(self.packed()):node.append(self.object())
  if flags&4:
   timelines=E.SubElement(node,'Timelines');named=E.SubElement(timelines,'NamedFrames')
   for _ in range(self.packed()):
    marker=E.SubElement(named,'NamedFrame');E.SubElement(marker,'Name').text=self.string(self.packed())
    E.SubElement(marker,'Time').text=str(self.packed());command=self.byte();assert command<5
    if command:E.SubElement(marker,'Command').text=['','stop','goto','gotoandplay','gotoandstop'][command]
    if command>1:E.SubElement(marker,'Parameter').text=self.string(self.packed())
    self.marker_count+=1
   if any(c.tag.startswith('Xui') for c in node):
    for _ in range(self.packed()):self.timeline(node,timelines)
  return node
 def timeline(self,owner,parent):
  target=self.string(self.packed());children=[n for n in owner if n.tag.startswith('Xui')]
  candidates=children+[n for child in children for n in list(child.iter())[1:]]
  node=next((n for n in candidates if n.findtext('Properties/Id')==target),None)
  assert node is not None,('Missing timeline target',target,self.at)
  classes=list(reversed(self.hierarchy(node.tag)));defs=[]
  timeline=E.SubElement(parent,'Timeline');E.SubElement(timeline,'Id').text=target
  for _ in range(self.packed()):
   depth=self.byte();is_indexed=bool(depth&128);depth&=127;class_index=self.byte();props=classes[class_index][1];names=[]
   for d in range(depth):
    property_index=self.byte();assert property_index<len(props),('Timeline property',target,node.tag,depth,class_index,property_index,len(props),self.at)
    p=props[property_index];names.append(p.get('Name'))
    if d<depth-1:props=self.classes[{'Fill':'XuiFigureFill','Gradient':'XuiFigureFillGradient','Stroke':'XuiFigureStroke'}[p.get('Name')]][1]
   index=self.packed() if is_indexed else None
   item=E.SubElement(timeline,'TimelineProp',{} if index is None else {'index':str(index)});item.text='.'.join(names);defs.append(p)
  self.timeline_count+=1
  key_total=self.packed();trace=[]
  for ki in range(key_total):
   key_start=self.at;frame=self.packed();raw_flag=self.byte();flag=raw_flag&0x3f
   assert flag in (0,1,2,3,10,11),(target,key_total,ki,frame,flag,self.at,trace[-4:])
   ease=[self.byte(),self.byte(),self.byte()] if flag==2 else [0,0,50]
   # V7 stores two additional packed easing coefficients after the legacy
   # byte parameters. Preserve these indices; their curve semantics are not
   # inferred from the later V8 writer.
   extra=[self.packed(),self.packed()] if flag in (2,10) else [self.byte()] if flag==11 else []
   key=E.SubElement(timeline,'KeyFrame');E.SubElement(key,'Time').text=str(frame);E.SubElement(key,'Interpolation').text=str(flag if flag<=2 else 0)
   E.SubElement(key,'OriginalFlag').text=str(raw_flag)
   for name,value in zip(['EaseIn','EaseOut','EaseScale'],ease):E.SubElement(key,name).text=str(value)
   if extra:E.SubElement(key,'OriginalEaseCoefficients').text=','.join(map(str,extra))
   for p in defs:
    values=self.property(p,False);assert len(values)==1 and isinstance(values[0],str);E.SubElement(key,'Prop').text=values[0]
   trace.append((key_start,frame,flag,[p.get('Name') for p in defs],self.at));self.key_count+=1
 def decode(self):
  off,size=self.parts['DATA'];self.at=off;node=self.object()
  assert self.at==off+size,('Unconsumed DATA',self.at,off+size)
  if self.counts:
   assert self.nodes==self.counts[0],('Node header mismatch',self.nodes,self.counts[0])
   assert self.key_count==self.counts[6],('Key header mismatch',self.key_count,self.counts[6])
   assert self.timeline_count==self.counts[7],('Timeline header mismatch',self.timeline_count,self.counts[7])
   assert self.marker_count==self.counts[8],('Marker header mismatch',self.marker_count,self.counts[8])
  root=E.Element('Xui');root.append(node);return root
def decode(source,schema,destination):
 errors=[]
 for ignore in (True,False):
  try:decoder=Decoder(source,schema,ignore);root=decoder.decode();break
  except (AssertionError,ValueError,IndexError,KeyError) as exc:errors.append(str(exc))
 else:raise ValueError('; '.join(errors))
 E.indent(root);E.ElementTree(root).write(destination,encoding='utf-8',xml_declaration=True)
 return dict(nodes=decoder.nodes,keys=decoder.key_count,tracks=decoder.timeline_count,markers=decoder.marker_count,data_consumed=True,header_counts_verified=decoder.counts is not None,schema_ignore_legacy_properties=ignore,curve_extensions_retained=True)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('schema',type=Path);p.add_argument('output',type=Path);a=p.parse_args();print(json.dumps(decode(a.source,a.schema,a.output)))
