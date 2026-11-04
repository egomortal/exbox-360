"""Render the native executable locally with deterministic controller scripts."""
from pathlib import Path
import json
import subprocess
from PIL import Image, ImageDraw
import argparse

ROOT=Path(__file__).resolve().parent
OUT=ROOT/"build/era-fidelity-pass2/final"
OUT.mkdir(parents=True,exist_ok=True)
def linux(path):
    return "/mnt/"+str(path)[0].lower()+str(path)[2:].replace("\\","/")
main=Path(r"C:\Users\USER\Downloads\playstation personal app\nxe-ps5\bigapp")
fixture=ROOT.parent/"test-fs"
if not fixture.exists():fixture=ROOT/"private/audit-test-fs"
media=ROOT/"private/trailers-native"
if not media.exists():media=ROOT/"private/trailers"
environment=["SDL_VIDEODRIVER=dummy","SDL_AUDIODRIVER=dummy",
    "NXE_FS_ROOT="+linux(fixture),"NXE_DEMO_APPS=12","NXE_ERA_SKIN_ROOT="+linux(ROOT/"private/era-skins"),"NXE_PROMOTION_ROOT="+linux(ROOT/"private/promotions"),
    "NXE_FONT_DIR="+linux(ROOT/"private/fonts-owner"),
    "NXE_RETAIL_LIBRARY="+linux(main/"private/xbox/unpacked"),
    "NXE_SHARED_LIBRARY="+linux(main/"private/xbox/shrdres"),
    "NXE_TRAILER_ROOT="+linux(media)+"/"]
cases={
 "blades-tray":"era:0,build:1888,channel:2,RT",
 "blades-tray-return":"era:0,build:1888,channel:2,RT,A,B",
 "blades-marketplace":"era:0,build:6770,channel:3",
 "blades-1888-live":"era:0,build:1888,channel:0",
 "blades-1888-media":"era:0,build:1888,channel:1",
 "blades-1888-system":"era:0,build:1888,channel:4",
 "blades-system":"era:0,build:6770",
 "blades-games":"era:0,build:6770,Left,Left",
 "blades-live":"era:0,build:6770,Left,Left,Left",
 "blades-media":"era:0,build:6770,Left",
 "blades-guide":"era:0,build:6770,Guide",
 "nxe-guide":"era:1,build:9199,Guide",
 "kinect-guide":"era:2,build:13604,Guide",
 "metro-v1-guide":"era:3,build:15574,Guide",
 "metro-v2-guide":"era:3,build:17559,Guide",
 "metro-guide-left":"era:3,build:17559,Guide,LB,LB,LB",
 "metro-guide-settings":"era:3,build:17559,Guide,RB,RB,RT",
 "picker-eras":"era:1,Guide,RB,RB,Down,Down,Down,Down,A",
 "picker-nxe":"era:1,Guide,RB,RB,Down,Down,Down,Down,A,A",
 "picker-preview":"era:1,Guide,RB,RB,Down,Down,Down,Down,A,X,A,LT",
 "picker-blades":"era:1,Guide,RB,RB,Down,Down,Down,Down,A,Up,A,LT",
 "picker-metro-v2":"era:1,Guide,RB,RB,Down,Down,Down,Down,A,RT,A",
 "metro-promos":"era:3,build:17559,promos:1",
 "nxe-promo":"era:1,build:9199,promos:1,RT",
 "kinect-promo":"era:2,build:13604,promos:1,RT",
}
cases.update({
 "blades-1888-games":"era:0,build:1888,Left,Left",
 "metro-v1-home":"era:3,build:15574,promos:1",
 "blades-guide-settings":"era:0,build:6770,Guide,Down,A",
 "picker-apply-v1":"era:1,build:9199,Guide,RB,RB,Down,Down,Down,Down,A,Down,Down,A,RT,A",
 "promo-click-gta":"era:3,build:17559,promos:1,A",
 "metro-v1-games":"era:3,build:15574,tab:3",
 "metro-v1-apps":"era:3,build:15574,tab:6",
 "metro-v2-apps":"era:3,build:17559,tab:6",
})
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--only")
parser.add_argument("--output",type=Path)
parser.add_argument("--video",action="store_true")
parser.add_argument("--source",action="store_true",help="Use the opt-in original XUI Blades desktop painter")
args=parser.parse_args()
if args.source:environment.append("NXE_BLADES_SOURCE=1")
if args.output:OUT=args.output.resolve();OUT.mkdir(parents=True,exist_ok=True)
if args.video:
    cases={"video-promo":"era:3,build:17559,promos:1", "video-blades":"era:0,Left,Left", "video-nxe":"era:1,Up,Up",
           "video-kinect":"era:2,Up,Up", "video-metro":"era:3,tab:3",
           "video-library":"era:3,tab:0,A,Down,Down,Down,Down",
           "video-fullscreen":"era:3,tab:4,A,RT,A"}
if args.only:cases={k:v for k,v in cases.items() if k in args.only.split(",")}
records=[]
for name,steps in cases.items():
    script="notify:0,"+steps+(",wait:3000,seek:20000,wait:600,capture:" if args.video else ",wait:850,snap:")+linux(OUT/(name if args.video else name+".bmp"))+",state:"+linux(OUT/(name+".json"))
    env=environment+["NXE_SCRIPT="+script,"NXE_FRAME_AUDIT="+linux(OUT/(name+"-timing.csv"))]
    if name=="blades-system":env.extend(["NXE_ART_AUDIT="+linux(OUT/"source-pixels.tsv"),"NXE_RAIL_AUDIT="+linux(OUT/"rail-layout.tsv")])
    result=subprocess.run(["wsl.exe","--cd",str(ROOT),"env",*env,"./build/exbox-host"],
        capture_output=True,text=True,timeout=75)
    (OUT/(name+".log")).write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(name+": "+result.stderr)
    bmp=OUT/(name+".bmp")
    im=Image.open(bmp).convert("RGB");im.save(OUT/(name+".png"));bmp.unlink()
    records.append({"scene":name,**json.loads((OUT/(name+".json")).read_text())})
    print(name,flush=True)
sheet=Image.new("RGB",(1920,390*((len(cases)+2)//3)),"#171717")
draw=ImageDraw.Draw(sheet)
for i,name in enumerate(cases):
    im=Image.open(OUT/(name+".png"));im.thumbnail((640,360),Image.Resampling.LANCZOS)
    x=(i%3)*640;y=(i//3)*390
    sheet.paste(im,(x,y+30));draw.text((x+12,y+8),name,fill="white")
sheet.save(OUT/("partial-contact-sheet.jpg" if args.only else "video-contact-sheet.jpg" if args.video else "contact-sheet.jpg"),quality=94)
(OUT/("partial-states.json" if args.only else "video-states.json" if args.video else "states.json")).write_text(json.dumps(records,indent=2)+"\n")
print("All local native scenes saved:",OUT,flush=True)
