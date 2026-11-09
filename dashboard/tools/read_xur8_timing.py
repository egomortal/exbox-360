"""Read independent V8 KEYD/NAME tables without guessing custom host classes."""
import struct,hashlib
class Reader:
 def __init__(self,data,at=0,end=None):self.data=data;self.at=at;self.end=len(data) if end is None else end
 def byte(self):
  assert self.at<self.end,'Truncated table';value=self.data[self.at];self.at+=1;return value
 def packed(self):
  value=self.byte()
  if value<240:return value
  if value<255:return ((value&15)<<8)|self.byte()
  return sum(self.byte()<<(24-8*i) for i in range(4))
def read(data):
 assert data[:8]==b'XUIB\0\0\0\x08'
 assert struct.unpack_from('>I',data,14)[0]==len(data)
 reader=Reader(data,20);counts=[reader.packed() for _ in range(12)];sections={}
 for _ in range(struct.unpack_from('>H',data,18)[0]):
  magic,offset,size=struct.unpack_from('>4sII',data,reader.at);reader.at+=12;assert offset+size<=len(data)
  sections[magic.decode()]=(offset,size)
 strings=['']
 if 'STRN' in sections:
  offset,size=sections['STRN'];n=struct.unpack_from('>H',data,offset+4)[0]
  strings+=data[offset+6:offset+size].decode('utf-8').rstrip('\0').split('\0');assert len(strings)==n+1
 keys=[];markers=[];extensions=[]
 if 'KEYD' in sections:
  offset,size=sections['KEYD'];r=Reader(data,offset,offset+size)
  while r.at<r.end:
   start=r.at;frame=r.packed();flag=r.byte();kind=flag&63;extra=[]
   if kind==2:extra=[r.byte(),r.byte(),r.byte()]
   elif kind==10:extra=[r.packed()]
   elif kind==11:extra=[r.byte()]
   # XUIHelper's V8 reader consumes no extra payload for these opaque
   # flag combinations. Retain every flag, then require exact table/header
   # counts. This establishes structural coverage, not curve semantics.
   else:pass
   property_index=r.packed();keys.append(dict(frame=frame,flag=flag,parameters=extra,property_index=property_index))
   if kind not in (0,1,2):extensions.append(dict(index=len(keys)-1,flag=flag,parameters=extra))
  assert len(keys)==counts[9],('KEYD header count',len(keys),counts[9])
 if 'NAME' in sections:
  offset,size=sections['NAME'];r=Reader(data,offset,offset+size)
  while r.at<r.end:
   name=strings[r.packed()];frame=r.packed();command=r.byte();assert command<5
   target=strings[r.packed()] if command>1 else ''
   markers.append(dict(name=name,frame=frame,command=command,target=target))
  assert len(markers)==counts[10],('NAME header count',len(markers),counts[10])
 return dict(keys=keys,markers=markers,curve_extensions=extensions,section_lengths_verified=True,header_counts_verified=True,curve_semantics_verified=False,structural_schema='XUIHelper V8 compatible payload lengths; opaque flags retained',
  sections={name:dict(offset=offset,bytes=size,sha256=hashlib.sha256(data[offset:offset+size]).hexdigest()) for name,(offset,size) in sections.items()})
