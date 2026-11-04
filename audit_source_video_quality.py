"""Measure JPEG pack encoding against the exact original decoded source frame."""
from pathlib import Path
from io import BytesIO
import json
import math
import struct
import subprocess
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parent
OUT=ROOT/"build/audit-20261025"
media=ROOT/"private/trailers-native"
if not media.exists():media=ROOT/"private/trailers"
source=media/"originals/fallout-4-xbox-one.mp4"
frame=600
def linux(path):return "/mnt/"+str(path)[0].lower()+str(path)[2:].replace("\\","/")
reference=OUT/"source-frame-600.png"
subprocess.run(["wsl.exe","--exec","ffmpeg","-hide_banner","-loglevel","error","-y","-i",linux(source),
    "-vf",f"fps=30,select=eq(n\\,{frame})","-frames:v","1",linux(reference)],check=True)
with (media/"packed/fallout-4-xbox-one.frames").open("rb") as file:
    file.read(8)
    for i in range(frame+1):
        length=struct.unpack("<I",file.read(4))[0]
        if i==frame:image=Image.open(BytesIO(file.read(length))).convert("RGB")
        else:file.seek(length,1)
image.save(OUT/"packed-frame-600.png")
a=np.asarray(image).astype(float);b=np.asarray(Image.open(reference).convert("RGB")).astype(float)
assert a.shape==b.shape==(1080,1920,3)
mse=float(np.mean((a-b)**2))
report={"source":"Official Xbox Fallout 4 HLS, 1080p, 30000/1001 fps", "output_frame":frame,
    "pack_resolution":[1920,1080],"jpeg_quality":2,
    "encoding_psnr_db":round(10*math.log10(255**2/mse),2),
    "mean_channel_delta":round(float(np.mean(np.abs(a-b))),3),
    "note":"JPEG is lossy; this measures encoding loss separately from viewport resizing."}
(OUT/"source-video-quality.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
