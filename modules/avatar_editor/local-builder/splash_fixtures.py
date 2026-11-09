"""Generate authored synthetic splash-loader fixtures. No original artwork."""
from pathlib import Path
import struct,zlib

def generate(root):
 root=Path(root);root.mkdir(parents=True,exist_ok=False)
 def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
 def png(w,h,color,pngtype=2):
  raw=(b'\0'+bytes(color)*w)*h
  return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,pngtype,0,0,0))+chunk(b'IDAT',zlib.compress(raw,1))+chunk(b'IEND',b'')
 for w,h,tag in [(1920,1080,'1080'),(3840,2160,'2160')]:
  for kind,color in [('override',(51,61,71)),('package',(17,31,59))]:
   (root/f'{kind}-{tag}.png').write_bytes(png(w,h,color))
 good=(root/'package-1080.png').read_bytes()
 corrupt=bytearray(good);corrupt[-1]^=1
 (root/'corrupt.png').write_bytes(corrupt)
 (root/'truncated.png').write_bytes(good[:-4])
 (root/'wrong-size.png').write_bytes(png(8,4,(17,31,59)))
 (root/'unsupported.png').write_bytes(png(1920,1080,(17,),pngtype=0))
 (root/'link.png').symlink_to(root/'package-1080.png')
 (root/'directory').mkdir()
 return root
