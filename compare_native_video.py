"""Compare final on-screen video crops against independently resized decoder pixels."""
from pathlib import Path
import csv
import json
import math
import numpy as np
from PIL import Image, ImageDraw

OUT=Path(__file__).resolve().parent/"build/audit-20261025/final"
results=[]
for path in sorted(OUT.glob("video-*-video.json")):
    name=path.name.removesuffix("-video.json")
    meta=json.loads(path.read_text())
    frame=Image.open(OUT/(name+"-frame.png")).convert("RGB")
    shot=Image.open(OUT/(name+".png")).convert("RGB")
    x,y,w,h=(meta[key] for key in ("x","y","width","height"))
    assert frame.size==(1920,1080),f"{name}: non-native decoder size {frame.size}"
    assert np.asarray(frame).std()>20 and np.asarray(frame).mean()>15,f"{name}: flat/fade frame cannot validate picture quality"
    assert meta["alpha"]==1 and w>0 and h>0 and abs(w-h*16/9)<2,f"{name}: aspect/fade"
    actual=shot.crop((x,y,x+w,y+h))
    independent=frame.resize((w,h),Image.Resampling.LANCZOS)
    a=np.asarray(actual).astype(float);b=np.asarray(independent).astype(float)
    delta=np.abs(a-b);mse=float(np.mean((a-b)**2))
    psnr=10*math.log10(255**2/mse) if mse else 100
    assert psnr>45,f"{name}: unexpected picture alteration ({psnr:.1f} dB)"
    timings=list(csv.DictReader((OUT/(name+"-timing.csv")).open()))
    processing=np.array([float(row["processing_ms"]) for row in timings
        if int(row["video_width"])==1920 and int(row["ticks"])>3000])
    record={"scene":name,"frame":meta["frame"],"decoder_size":frame.size,"viewport":[x,y,w,h],
        "independent_lanczos_psnr_db":round(psnr,2),"mean_channel_delta":round(float(delta.mean()),3),
        "p99_channel_delta":round(float(np.percentile(delta,99)),2),
        "processing_ms_median":round(float(np.median(processing)),2),
        "processing_ms_p95":round(float(np.percentile(processing,95)),2),
        "frames_measured":len(processing)}
    results.append(record)
    actual.save(OUT/(name+"-actual-crop.png"))
    independent.save(OUT/(name+"-independent-crop.png"))
(OUT.parent/"video-pixel-comparison.json").write_text(json.dumps(results,indent=2)+"\n")
print(json.dumps(results,indent=2))
