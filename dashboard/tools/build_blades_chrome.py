"""Bake lossless 1080p Blades materials; never resample the rail source images.

Private reconstruction reference: ZivvoZ/dashx360 at
0c8adb2cef4ad7dff2bf889351efa1f1c244aef2, Assets/Blades/StatePngs.
These are recreation assets, not claimed to be Microsoft's original bitmaps.
The early four-blade variant removes the 50px Marketplace strip from the left
stack only. Original designer captures remain the authority for content anchors.
"""
from pathlib import Path
import argparse, hashlib, json
import numpy as np
from PIL import Image

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("destination", type=Path)
parser.add_argument("--reference-dir",type=Path,help="Original designer captures for material color sampling")
parser.add_argument("--lightfield-root",type=Path,help="Optional dense empty-background lighting calibration")
parser.add_argument("--body-only",action="store_true",help="Save the existing recreated material without any third-party rails")
parser.add_argument("--only-active",type=int,choices=range(5),help="Limit this conversion to one blade material")
args=parser.parse_args()
args.destination.mkdir(parents=True, exist_ok=True)
names=["marketplace", "xbox-live", "games", "media", "system"]
# Broad, desaturated material rather than a neon spotlight. Measured against
# designer-blades-{live,games,music,system}; colors remain an approximation.
colors=[(212,112,61),(233,172,72),(105,183,84),(76,150,199),(154,123,193)]
y,x=np.mgrid[0:1080,0:1920].astype(np.float32)
records=[]
color_records=[]
def sampled_material(name):
    reference={"xbox-live":"live","games":"games","media":"music","system":"system"}.get(name)
    if not reference or not args.reference_dir:return None
    source=args.reference_dir/f"designer-blades-{reference}.jpg"
    im=np.asarray(Image.open(source).convert("RGB").resize((1920,1080),Image.Resampling.LANCZOS))
    # Empty background patches; exclude profiles, text, logos, banners and tray.
    points=[(900,30),(900,80),(1450,200),(1450,300),(975,365),
            (1000,650),(1450,650),(1250,730),(650,765),(1000,765),
            (900,890),(650,950),(1100,990),(900,1050)]
    anchors=[(px,py,np.median(im[py-4:py+5,px-4:px+5],axis=(0,1))) for px,py in points]
    fields=[]
    for lo,hi in [(0,132),(132,895),(895,1080)]:
        numer=np.zeros((1080,1920,3),np.float32);denom=np.zeros((1080,1920),np.float32)
        for px,py,color in anchors:
            if not lo<=py<hi:continue
            weight=1/(((x-px)/700)**2+((y-py)/320)**2+0.00001)**2
            numer+=weight[:,:,None]*color;denom+=weight
        fields.append(numer/denom[:,:,None])
    color_records.append(dict(material=name,reference=str(source),
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        samples=[dict(x=px,y=py,rgb=[int(v) for v in c]) for px,py,c in anchors]))
    return fields
for active,name in enumerate(names):
    if args.only_active is not None and active!=args.only_active:continue
    source=args.source/(name+"-blades.png")
    rail=Image.open(source).convert("RGBA")
    if rail.size!=(1920,1080): raise ValueError("Unexpected rail dimensions")
    sampled=sampled_material(name)
    for first in ([0] if active==0 else [0,1]):
        early=bool(first)
        left=202+74*(active-1)+(0 if early else 50)
        right=1498+74*(active-1)
        base=np.broadcast_to(np.array(colors[active],np.float32),(1080,1920,3)).copy()
        glow=np.exp(-((x-(left+right)*.5)/720)**2-((y-320)/540)**2)
        base+=glow[:,:,None]*np.array([14,18,9])
        base-=(y/1080)[:,:,None]*np.array([11,10,8])
        # Rounded glass header and footer with a narrow specular seam.
        inset=np.minimum(np.maximum(x-left,0),np.maximum(right-x,0))
        turn=1-np.sqrt(np.maximum(0,1-np.clip(1-inset/54,0,1)**2))
        header=132-30*turn
        footer=895+24*turn
        for opacity,falloff in [(np.clip(header-y,0,1), .72+.20*y/132), (np.clip(y-footer,0,1),.74+.18*(y-895)/185)]:
            base*=1-opacity[:,:,None]*(1-falloff[:,:,None])
        # Broad curved reflections retain variation even inside the glass.
        wave_y=210+110*np.sin((x-left)/1100*np.pi)
        wave=np.exp(-((y-wave_y)/100)**2)*9-np.exp(-((y-wave_y-145)/120)**2)*7
        base+=wave[:,:,None]
        if sampled is not None:
            # This is a color-field reconstruction from empty patches, not a
            # screenshot with retail controls pasted into the native interface.
            base=sampled[1].copy()
            for opacity,field in [(np.clip(header-y,0,1),sampled[0]),(np.clip(y-footer,0,1),sampled[2])]:
                base=base*(1-opacity[:,:,None])+field*opacity[:,:,None]
        if args.lightfield_root and (args.lightfield_root/f'lightfield-{active}.png').exists():
            base=np.asarray(Image.open(args.lightfield_root/f'lightfield-{active}.png').convert('RGB')).astype(np.float32).copy()
        seam=np.exp(-((y-header)/1.2)**2)*.22+np.exp(-((y-footer)/1.3)**2)*.24
        base+=seam[:,:,None]*35
        # Curved colored jewel beside the selected blade, with gloss and its
        # bottom reflection. The grey rail image supplies its actual contour.
        jewel=left+28+30*np.sin(np.clip(y/1080,0,1)*np.pi)
        d=x-jewel
        ridge=(np.exp(-((d-8)/18)**2)*.25-np.exp(-((d-45)/6)**2)*.30)
        base*=1+ridge[:,:,None]
        haze=(np.exp(-((x-left-28-(y-780)*.36)/28)**2-((y-800)/95)**2)*.20)
        base+=haze[:,:,None]*150
        frame=Image.fromarray(np.uint8(np.clip(base,0,255)),"RGB").convert("RGBA")
        layer=rail
        if early:
            layer=Image.new("RGBA",rail.size)
            layer.paste(rail.crop((50,0,960,1080)),(0,0))
            layer.paste(rail.crop((960,0,1920,1080)),(960,0))
        if not args.body_only:frame.alpha_composite(layer)
        prefix="body-" if args.body_only else ""
        target=args.destination/f"{prefix}{'4' if early else '5'}-{active}.png"
        frame.convert("RGB").save(target)
        records.append(dict(file=target.name,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
            classification="recreated-calibrated-body" if args.body_only else "recreated-chrome",
            rail_composited=not args.body_only,rail_source=str(source),rail_sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
(args.destination/"sources.json").write_text(json.dumps(dict(
    reference="https://github.com/ZivvoZ/dashx360/tree/0c8adb2cef4ad7dff2bf889351efa1f1c244aef2",
    credit="Xbox / Microsoft; ZivvoZ / DashX360 reconstruction", files=records),indent=2)+"\n")
(args.destination/"color-samples.json").write_text(json.dumps(color_records,indent=2)+"\n")
print(f"Wrote {len(records)} lossless Blades backgrounds")
