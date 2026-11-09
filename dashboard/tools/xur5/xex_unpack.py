"""Unpack an UNENCRYPTED XEX2 (encryption type 0) with normal (LZX) compression
and dump its embedded resources. Encrypted modules are refused."""
import struct,sys,pathlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from lzx import Bits,Decoder
def u32(d,o):return struct.unpack_from('>I',d,o)[0]
def unpack(path,out):
    d=pathlib.Path(path).read_bytes();assert d[:4]==b'XEX2'
    pe=u32(d,8);sec=u32(d,0x10);n=u32(d,0x14);hdr={}
    for i in range(n):
        k,v=struct.unpack_from('>II',d,0x18+8*i);hdr[k]=v
    image_size=u32(d,sec+4)
    ff=hdr[0x3ff];enc,comp=struct.unpack_from('>HH',d,ff+4)
    if enc!=0:raise SystemExit('encrypted module: refused')
    if comp==1:  # basic: (data,zero) pairs
        info=u32(d,ff);img=bytearray();p=pe
        for i in range((info-8)//8):
            dz,zz=struct.unpack_from('>II',d,ff+8+8*i);img+=d[p:p+dz]+bytes(zz);p+=dz
    else:
        assert comp==2,comp
        window=u32(d,ff+8);block=u32(d,ff+12);p=pe;stream=bytearray()
        while block:
            nxt=u32(d,p);q=p+24
            while True:
                c=struct.unpack_from('>H',d,q)[0];q+=2
                if not c:break
                stream+=d[q:q+c];q+=c
            p+=block;block=nxt
        dec=Decoder(window.bit_length()-1);bits=Bits(bytes(stream),0);img=bytearray()
        while len(img)<image_size:
            img+=dec.decode(bits,min(32768,image_size-len(img)));dec.frame_end(bits)
    img=bytes(img[:image_size])
    base=hdr.get(0x10201) or u32(d,sec+0x110)
    o=pathlib.Path(out);o.mkdir(parents=True,exist_ok=True)
    (o/'basefile.bin').write_bytes(img)
    if 0x2ff in hdr:
        r=hdr[0x2ff];size=u32(d,r);cnt=(size-4)//16
        for i in range(cnt):
            name,addr,sz=struct.unpack_from('>8sII',d,r+4+16*i)
            name=name.rstrip(b'\0').decode();off=addr-base
            (o/name).write_bytes(img[off:off+sz]);print(name,hex(addr),sz,img[off:off+4])
if __name__=='__main__':unpack(sys.argv[1],sys.argv[2])
