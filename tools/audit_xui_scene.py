"""Capture the native original-XUI desktop preview without a console connection."""
from pathlib import Path
import argparse,json,re,subprocess,xml.etree.ElementTree as ET
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--assets',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
def linux(p):return '/mnt/'+str(p)[0].lower()+str(p)[2:].replace('\\','/')
canonical=Path(r'C:\Users\USER\Downloads\playstation personal app\nxe-ps5\bigapp')
env=['SDL_VIDEODRIVER=dummy','SDL_AUDIODRIVER=dummy','NXE_BLADES_SOURCE=1',
    'NXE_FS_ROOT='+linux(ROOT.parent/'test-fs'),'NXE_TRAILER_ROOT='+linux(out/'no-trailers')+'/','NXE_ERA_SKIN_ROOT='+linux(ROOT/'private/era-skins'),
    'NXE_FONT_DIR='+linux(ROOT/'private/fonts-owner'),'NXE_RETAIL_LIBRARY='+linux(canonical/'private/xbox/unpacked'),
    'NXE_SHARED_LIBRARY='+linux(canonical/'private/xbox/shrdres')]
records=[]
for build,xml,channels in [(1888,'blades-main.xui',[0,2,1,4]),(6717,'blades-main-6717.xui',[3,0,2,1,4])]:
    scene=ET.parse(a.assets/'scene-audit'/xml).getroot().find('XuiTabScene')
    frames={n.findtext('Name'):float(n.findtext('Time')) for n in scene.findall('Timelines/NamedFrames/NamedFrame')}
    steps=['notify:0','era:0','build:'+str(build),'wait:1600']
    transitions=[]
    for name,start in frames.items():
        match=re.fullmatch(r'(\d)To(\d)',name)
        if not match:continue
        end=frames[name+'End'];target=int(match[2])-1
        samples=[start,(start+end)/2,end]
        # Dense, lossless animation samples for one transition in each family.
        if name=='2To3':samples=sorted(set(samples+[float(n) for n in range(int(start),int(end)+1,2)]))
        captures=[];steps.append('channel:'+str(channels[target]))
        for index,frame in enumerate(samples):
            stem=f'{build}-{name}-{index:02d}'
            steps+=['xui-frame:'+str(frame),'xui-loop:0','snap:'+linux(out/(stem+'.bmp'))]
            captures.append(dict(file=stem+'.png',frame=frame))
        transitions.append(dict(name=name,start=start,end=end,duration_ms=(end-start)*1000/60,captures=captures))
    # Drive the same native navigation wrapper through a queued/reversed goal.
    steps+=['channel:'+str(channels[0]),'wait:650','channel:'+str(channels[-1]),'channel:'+str(channels[1]),'wait:2100',
        'xui-state:'+linux(out/f'{build}-navigation.json')]
    result=subprocess.run(['wsl.exe','--cd',str(ROOT),'env',*env,'NXE_SCRIPT='+','.join(steps),'./build/exbox-host'],capture_output=True,text=True,timeout=180)
    (out/f'{build}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:raise RuntimeError(result.stderr)
    for t in transitions:
        for capture in t['captures']:
            png=out/capture['file'];bmp=png.with_suffix('.bmp');image=Image.open(bmp);image.save(png);bmp.unlink()
        if t['name']=='2To3':
            ims=[Image.open(out/c['file']).convert('RGB') for c in t['captures']]
            durations=[max(1,round((t['captures'][i+1]['frame']-t['captures'][i]['frame'])*1000/60)) for i in range(len(ims)-1)]+[700]
            ims[0].save(out/f'{build}-{t["name"]}.webp',save_all=True,append_images=ims[1:],duration=durations,loop=0,lossless=True,method=4)
    navigation=json.loads((out/f'{build}-navigation.json').read_text())
    assert navigation['current']==(2 if build==1888 else 1) and not navigation['transitioning'],navigation
    records.append(dict(build=build,transitions=transitions,navigation=navigation))
(out/'captures.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
print(json.dumps([dict(build=r['build'],transitions=len(r['transitions']),frames=sum(len(t['captures']) for t in r['transitions']),navigation_passed=True,render_us=r['navigation']['render_us']) for r in records],indent=2))

