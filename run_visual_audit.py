"""Render the native executable locally with deterministic controller scripts."""
from pathlib import Path
import json
import subprocess
from PIL import Image, ImageDraw
import argparse

ROOT=Path(__file__).resolve().parent
OUT=ROOT/"build/audit-20261025/final"
OUT.mkdir(parents=True,exist_ok=True)
def linux(path):
    return "/mnt/"+str(path)[0].lower()+str(path)[2:].replace("\\","/")
main=Path(r"C:\Users\USER\Downloads\playstation personal app\nxe-ps5\bigapp")
fixture=ROOT.parent/"test-fs"
if not fixture.exists():fixture=ROOT/"private/audit-test-fs"
media=ROOT/"private/trailers-native"
if not media.exists():media=ROOT/"private/trailers"
environment=["SDL_VIDEODRIVER=dummy","SDL_AUDIODRIVER=dummy",
    "NXE_FS_ROOT="+linux(fixture),"NXE_DEMO_APPS=12",
    "NXE_FONT_DIR="+linux(ROOT/"private/fonts-owner"),
    "NXE_RETAIL_LIBRARY="+linux(main/"private/xbox/unpacked"),
    "NXE_SHARED_LIBRARY="+linux(main/"private/xbox/shrdres"),
    "NXE_TRAILER_ROOT="+linux(media)+"/"]
cases={
 "blades-system":"era:0",
 "blades-media":"era:0,Left",
 "blades-games":"era:0,Left,Left",
 "blades-system-last":"era:0,RT",
 "nxe-home":"era:1",
 "nxe-games":"era:1,Up,Up",
 "kinect-home":"era:2",
 "kinect-games":"era:2,Up,Up",
 "metro-home":"era:3",
 "metro-tabs":"era:3,Up,Right,Right",
 "metro-games":"era:3,tab:3",
 "metro-last-page":"era:3,tab:6,RT",
 "metro-pins-focus":"era:3,tab:3,Left",
 "metro-pins-empty":"era:3,tab:3,Left,A",
 "metro-search":"era:3,tab:0,A",
 "metro-settings":"era:3,tab:7,A",
 "game-details":"era:3,tab:3,Y",
 "guide-home":"era:1,Guide",
 "guide-settings":"era:1,Guide,RB,RB,RB,RB",
 "toast-nxe":"era:1,toast,wait:1400",
 "buttons-glossy":"era:1,buttons:0",
 "buttons-retail":"era:1,buttons:1",
 "buttons-simple":"era:1,buttons:2",
 "map-glossy":"era:1,buttons:0,RT,A",
 "map-retail":"era:1,buttons:1,RT,A",
 "map-simple":"era:1,buttons:2,RT,A",
 "theme-blue":"era:1,theme:1",
 "theme-purple":"era:1,theme:2",
}
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--only")
parser.add_argument("--video",action="store_true")
args=parser.parse_args()
if args.video:
    cases={"video-blades":"era:0,Left,Left", "video-nxe":"era:1,Up,Up",
           "video-kinect":"era:2,Up,Up", "video-metro":"era:3,tab:3",
           "video-library":"era:3,tab:0,A,Down,Down,Down,Down",
           "video-fullscreen":"era:3,tab:4,A,RT,A"}
if args.only:cases={k:v for k,v in cases.items() if k in args.only.split(",")}
records=[]
for name,steps in cases.items():
    script="notify:0,"+steps+(",wait:3000,seek:20000,wait:600,capture:" if args.video else ",wait:850,snap:")+linux(OUT/(name if args.video else name+".bmp"))+",state:"+linux(OUT/(name+".json"))
    env=environment+["NXE_SCRIPT="+script,"NXE_FRAME_AUDIT="+linux(OUT/(name+"-timing.csv"))]
    if name=="blades-system":env.append("NXE_ART_AUDIT="+linux(OUT/"source-pixels.tsv"))
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
