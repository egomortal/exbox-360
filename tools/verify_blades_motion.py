"""Check native shine motion and ensure it stays inside the selected control."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from PIL import Image

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
def linux(path):return '/mnt/'+str(path)[0].lower()+str(path)[2:].replace('\\','/')
main=Path(r'C:\Users\USER\Downloads\playstation personal app\nxe-ps5\bigapp')
env=['SDL_VIDEODRIVER=dummy','SDL_AUDIODRIVER=dummy','NXE_MATERIAL_TRACE=1','NXE_FS_ROOT='+linux(root.parent/'test-fs'),
    'NXE_ERA_SKIN_ROOT='+linux(root/'private/era-skins'),'NXE_FONT_DIR='+linux(root/'private/fonts-owner'),
    'NXE_RETAIL_LIBRARY='+linux(main/'private/xbox/unpacked'),'NXE_SHARED_LIBRARY='+linux(main/'private/xbox/shrdres')]
records=[]
for name,commands,region in [('row','channel:1',(423,375,584,74)),('tray','channel:2,RT',(333,782,1193,105))]:
    # Each phase starts separately: saving an 8MB BMP through WSL can pause
    # the app long enough to miss a later shine peak in the same process.
    for phase,delay in [('before',100),('peak',700),('after',1400)]:
        steps=['notify:0','era:0','build:1888',commands,'wait:'+str(delay),'snap:'+linux(out/f'{name}-{phase}.bmp')]
        result=subprocess.run(['wsl.exe','--cd',str(root),'env',*env,'NXE_SCRIPT='+','.join(steps),'./build/exbox-host'],capture_output=True,text=True,timeout=45)
        (out/f'{name}-{phase}.log').write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(result.stderr)
    images=[]
    for phase in ['before','peak','after']:
        path=out/f'{name}-{phase}.bmp';im=Image.open(path).convert('RGB');im.save(path.with_suffix('.png'));path.unlink();images.append(np.asarray(im).astype(int))
    delta=np.max(np.abs(images[0]-images[1]),axis=2)>1
    x,y,w,h=region;inside=np.zeros(delta.shape,bool);inside[y:y+h,x:x+w]=True
    moved=int((delta&inside).sum());outside=int((delta&~inside).sum())
    if moved<100 or outside:raise AssertionError(dict(control=name,changed=moved,outside=outside))
    records.append(dict(control=name,changed_pixels=moved,outside_clip=outside,passed=True))
(out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records,indent=2))
