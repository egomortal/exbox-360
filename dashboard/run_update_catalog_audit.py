"""Capture Home, Guide and pixel-identity evidence for every catalog update locally."""
import os
from pathlib import Path
from PIL import Image,ImageDraw
import json,subprocess,argparse

ROOT=Path(__file__).resolve().parent
ASSET=Path(os.environ.get('EXBOX_ASSET_DIR','private/asset/exbox360'))
OUT=ROOT/'build/era-fidelity-pass2/updates'
OUT.mkdir(parents=True,exist_ok=True)
def linux(path):return '/mnt/'+str(path)[0].lower()+str(path)[2:].replace('\\','/')
main=ROOT
env=['SDL_VIDEODRIVER=dummy','SDL_AUDIODRIVER=dummy','NXE_FS_ROOT='+linux(ROOT.parent/'test-fs'),
 'NXE_DEMO_APPS=12','NXE_ERA_SKIN_ROOT='+linux(ROOT/'private/era-skins'),
 'NXE_PROMOTION_ROOT='+linux(ROOT/'private/promotions'),'NXE_FONT_DIR='+linux(ROOT/'private/fonts-owner'),
 'NXE_RETAIL_LIBRARY='+linux(main/'private/xbox/unpacked'),'NXE_SHARED_LIBRARY='+linux(main/'private/xbox/shrdres')]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--family',type=int,choices=range(5));parser.add_argument('--build',type=int);parser.add_argument('--output',type=Path);args=parser.parse_args()
if args.output:OUT=args.output.resolve();OUT.mkdir(parents=True,exist_ok=True)
updates=json.loads((ASSET/'360dasharchive/update-catalog.json').read_text())['updates']
if args.family is not None:updates=[u for u in updates if u['family']==args.family]
if args.build is not None:updates=[u for u in updates if u['build']==args.build]
previous=json.loads((OUT/'results.json').read_text()) if (args.family is not None or args.build is not None) and (OUT/'results.json').exists() else []
replaced={u['build'] for u in updates};records=[r for r in previous if r['build'] not in replaced]
for start in range(0,len(updates),6):
    batch=updates[start:start+6];steps=['notify:0']
    for update in batch:
        build=update['build'];era=min(update['family'],3)
        steps+=['era:'+str(era),'build:'+str(build),'channel:'+str(2 if era==0 else 4),'LT','promos:1',
            'wait:400','snap:'+linux(OUT/f'{build}-home.bmp'),'state:'+linux(OUT/f'{build}-home.json'),
            'pixels:'+linux(OUT/f'{build}-pixels.tsv'),'Guide','wait:400',
            'snap:'+linux(OUT/f'{build}-guide.bmp'),'state:'+linux(OUT/f'{build}-guide.json'),'Guide','wait:350']
    result=subprocess.run(['wsl.exe','--cd',str(ROOT),'env',*env,'NXE_SCRIPT='+','.join(steps),'./build/exbox-host'],capture_output=True,text=True,timeout=150)
    (OUT/f'batch-{start}.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    for update in batch:
        build=update['build']
        for view in ['home','guide']:
            bmp=OUT/f'{build}-{view}.bmp';im=Image.open(bmp).convert('RGB');im.save(bmp.with_suffix('.png'));bmp.unlink()
            state=json.loads((OUT/f'{build}-{view}.json').read_text())
            if state['build']!=build or state['era']!=min(update['family'],3) or bool(state['guide'])!=(view=='guide'):raise ValueError('Incorrect captured state')
        pixel_rows=(OUT/f'{build}-pixels.tsv').read_text().splitlines()[1:]
        for row in pixel_rows:
            columns=row.split('\t')
            if columns[0].startswith(('category-','era-','material-')) and (len(columns)!=7 or columns[-2:]!=['0','0']):raise ValueError('Era image/material changed: '+row)
        records.append({**update,'local_render':'pass','home':f'{build}-home.png','guide':f'{build}-guide.png','source_identity':'pass',
            'historical_pixel_match':'not asserted: custom native actions and reconstructed chrome remain'})
        print(build,'Home + Guide + source identity PASS',flush=True)
records.sort(key=lambda r:(r['family'],r['build']))
(OUT/'results.json').write_text(json.dumps(records,indent=2)+'\n')
families=['Blades','NXE','Kinect','Metro V1','Metro V2']
page=['<!doctype html><meta charset="utf-8"><title>Ex-Box local update audit</title><style>body{background:#17191a;color:#eee;font:16px system-ui;margin:30px}section{display:grid;grid-template-columns:1fr 1fr;gap:12px}img{width:100%}article{margin:30px 0}a{color:#b5e582}summary{padding:14px;background:#303538}p{max-width:1000px}</style><h1>Archived updates — native local capture audit</h1><p>Home and Guide renders with recovered build assets. These captures verify local rendering and image identity; they do not claim every preview is historically pixel exact. Campaign provenance and unverified geometry are recorded separately.</p>']
for family,name in enumerate(families):
    group=[r for r in records if r['family']==family]
    if not group:continue
    sheet=Image.new('RGB',(1280,390*((len(group)+1)//2)),'#17191a');draw=ImageDraw.Draw(sheet)
    for i,r in enumerate(group):
        image=Image.open(OUT/r['home']);image.thumbnail((640,360),Image.Resampling.LANCZOS)
        x=i%2*640;y=i//2*390;sheet.paste(image,(x,y+30));draw.text((x+10,y+8),str(r['build'])+(' PREVIEW' if r['preview'] else ''),fill='white')
    sheet.save(OUT/f'family-{family}.jpg',quality=95)
    page.append('<h2>'+name+'</h2>')
    for r in group:
        label=str(r['build'])+(' — preview' if r['preview'] else ' — retail')
        page.append('<details><summary>'+label+' — rendered / source pixels unchanged</summary><section>'+''.join('<a href="'+r[v]+'"><img loading="lazy" src="'+r[v]+'"></a>' for v in ['home','guide'])+'</section><a href="'+str(r['build'])+'-pixels.tsv">Pixel identity report</a></details>')
(OUT/'index.html').write_text('\n'.join(page),encoding='utf-8')
print(len(records),'updates captured; no console contacted',flush=True)
