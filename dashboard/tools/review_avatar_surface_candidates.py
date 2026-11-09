from pathlib import Path
import sys,json,subprocess
from PIL import Image,ImageDraw
r=Path(__file__).resolve().parents[1];w=r/'private/avatar/manual-geometry/surface-fairing'
names=['b1-mv-profile','b2-mv','b3-mv','cereza-mv']
sheet=Image.new('RGB',(2048,1440),'#d2e1ca');draw=ImageDraw.Draw(sheet)
for i,name in enumerate(names):
 p=w/name;report=json.loads((p/'controls-and-validation.json').read_text());base=r/'private/avatar/multiview/cleaned'/name
 # Repackage at the same app height as the source to ensure comparable renders.
 if i<3:
  subprocess.run([sys.executable,str(r/'tools/import_image_model.py'),str(r/'private/avatar/kit360.base.nxak'),str(p/'model.obj'),report['texture'],str(p/'candidate.nxak'),'--label',name+' surface candidate','--height','1.73','--mirror-x'],check=True)
  subprocess.run(['/tmp/render_image_model_mv',str(p/'candidate.nxak'),str(p/'frames'),'24','1'],check=True,stdout=subprocess.DEVNULL)
 box=report['face_boundary'];xmin=min(x for x,y in box)-256-30;xmax=max(x for x,y in box)-256+30;ymin=min(y for x,y in box)-30;ymax=max(y for x,y in box)+30
 before=Image.open(base/'guide.png' if i else r/'private/avatar/manual-geometry/b1/rebuilt-guide.png');after=Image.open(p/'guide.png')
 for j,im in enumerate([before,after]):
  crop=im.crop((xmin,ymin,xmax,ymax));crop.thumbnail((490,330));sheet.paste(crop,(i*512+(512-crop.width)//2,j*360+25),crop)
  draw.text((i*512+8,j*360+7),name+(' before' if j==0 else ' depth-faired'),fill='black')
 # Side material review uses real orthographic renders, not a reference overlay.
 for j,col in enumerate([1,3]):
  crop=after.crop((col*512,100,(col+1)*512,400));crop.thumbnail((512,340));sheet.paste(crop,(i*512,720+j*360+25),crop);draw.text((i*512+8,720+j*360+7),'Side '+str(col),fill='black')
 report['candidate_sha256']=__import__('hashlib').sha256((p/'candidate.nxak').read_bytes()).hexdigest();report['rendered_frames']=24
 (p/'controls-and-validation.json').write_text(json.dumps(report,indent=2))
sheet.save(w/'head-review.png')
