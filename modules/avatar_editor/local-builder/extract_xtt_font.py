"""Convert the owner's compressed Xbox XTT outlines to a standard TrueType container."""
from pathlib import Path
import argparse
import struct
import zlib

def checksum(data):
    data=data+b"\0"*((-len(data))%4)
    return sum(struct.unpack(">"+"I"*(len(data)//4),data))&0xffffffff

def convert(source,target):
    raw=source.read_bytes()
    if raw[:4]!=b"xttf": raise ValueError("Not an Xbox XTT file")
    metadata=None
    for start in range(260,min(512,len(raw))):
        try:
            candidate=zlib.decompress(raw[start:])
            if candidate[:4]==b"\0\1\0\0": metadata=candidate;break
        except zlib.error: pass
    if metadata is None: raise ValueError("Compressed font directory not found")
    count=struct.unpack_from(">H",metadata,4)[0]
    records={}
    for i in range(count):
        tag,_,offset,length=struct.unpack_from(">4sIII",metadata,12+i*16)
        records[tag]=(offset,length)
    def table(tag):
        offset,length=records[tag]
        return metadata[offset:offset+length]
    locations=struct.unpack(">"+"I"*(len(table(b"xloc"))//4),table(b"xloc"))
    glyph_count=len(locations)-1
    page_count=(locations[-1]>>16)+1
    page_start=records[b"xglf"][0]
    pages=[];bases=[];size=0
    for page in range(page_count):
        bases.append(size)
        data=zlib.decompress(raw[page_start+page*4096:page_start+(page+1)*4096])
        pages.append(data);size+=len(data)
    glyf=b"".join(pages)
    offsets=[bases[loc>>16]+(loc&65535) for loc in locations]
    if offsets!=sorted(offsets) or offsets[-1]!=len(glyf): raise ValueError("Invalid glyph offsets")
    max_points=max_contours=max_instructions=0
    for i in range(glyph_count):
        glyph=glyf[offsets[i]:offsets[i+1]]
        if not glyph: continue
        contours=struct.unpack_from(">h",glyph)[0]
        if contours>=0:
            points=struct.unpack_from(">H",glyph,10+(contours-1)*2)[0]+1 if contours else 0
            instructions=struct.unpack_from(">H",glyph,10+contours*2)[0]
            max_points=max(max_points,points);max_contours=max(max_contours,contours)
            max_instructions=max(max_instructions,instructions)
    tables={tag:table(tag) for tag in (b"cmap",b"head",b"hhea",b"hmtx",b"name")}
    head=bytearray(tables[b"head"]);head[8:12]=b"\0"*4;struct.pack_into(">h",head,50,1);tables[b"head"]=bytes(head)
    tables[b"glyf"]=glyf
    tables[b"loca"]=struct.pack(">"+"I"*len(offsets),*offsets)
    tables[b"maxp"]=struct.pack(">I14H",0x10000,glyph_count,max_points,max_contours,
        min(65535,max_points*32),min(65535,max_contours*32),2,0,256,0,0,1024,max_instructions,32,16)
    tables[b"post"]=struct.pack(">IIhhIIIII",0x30000,0,0,0,0,0,0,0,0)
    tags=sorted(tables);n=len(tags);power=2**(n.bit_length()-1)
    header=struct.pack(">IHHHH",0x10000,n,power*16,power.bit_length()-1,n*16-power*16)
    directory=bytearray();payload=bytearray();offset=12+n*16;head_at=None
    for tag in tags:
        data=tables[tag]
        directory.extend(struct.pack(">4sIII",tag,checksum(data),offset,len(data)))
        if tag==b"head": head_at=offset
        padding=(-len(data))%4
        payload.extend(data+b"\0"*padding);offset+=len(data)+padding
    result=bytearray(header+directory+payload)
    struct.pack_into(">I",result,head_at+8,(0xb1b0afba-checksum(result))&0xffffffff)
    if checksum(result)!=0xb1b0afba: raise ValueError("Font checksum failed")
    target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(result)
    print(f"{glyph_count} glyphs, {page_count} outline pages, {len(result)} bytes; source outlines preserved")

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source",type=Path);parser.add_argument("target",type=Path)
    args=parser.parse_args();convert(args.source,args.target)
