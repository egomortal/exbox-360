from pathlib import Path
import sys,tempfile
import numpy as np
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools'))
from import_image_model import make_part
from expand_bayonetta import parse_kit
kit=parse_kit((r/'private/avatar/kit360.base.nxak').read_bytes());ibm=kit['parts'][1][3]
v=np.array([[-.3,0,-.1],[.4,.2,0],[0,1.7,.2],[.1,.8,-.3]],float)
with tempfile.TemporaryDirectory() as tmp:
 def write(name,vertices):
  path=Path(tmp)/name
  path.write_text(''.join('v %.8f %.8f %.8f\n'%tuple(p) for p in vertices)+'vt 0 0\nvt 1 0\nvt .5 1\nvt .2 .4\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\nf 2/2 4/4 3/3\n');return path
 standard=write('standard.obj',v);legacy=write('legacy.obj',v[:,[2,0,1]])
 for mirror in (False,True):
  a=make_part(legacy,0,ibm,'test',1.73,mirror,'legacy');b=make_part(standard,0,ibm,'test',1.73,mirror,'y-up');assert a==b
 flat=v.copy();flat[:,1]=0
 try:make_part(write('flat.obj',flat),0,ibm,'test',source_axes='y-up')
 except ValueError as error:assert 'nonzero height' in str(error)
 else:raise AssertionError('Zero-height input accepted')
print('PASS: standard-coordinate and legacy imports are byte-identical with both mirror settings; zero-height input rejected.')
