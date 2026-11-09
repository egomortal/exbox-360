"""Measure and visualize front silhouette alignment with the original concept.

Usage: python tools/audit_image_model_alignment.py FRONT_RENDER.ppm OUTPUT.png [SOURCE_CUTOUT.png]
"""
from pathlib import Path
import sys
import numpy as np
import cv2
from PIL import Image, ImageDraw

project = Path(__file__).resolve().parent.parent
source_path = (Path(sys.argv[3]) if len(sys.argv) > 3 else
               project / "private/avatar/bayonetta1-original-extracted.png")
source_rgba = np.asarray(Image.open(source_path).convert("RGBA"))
render = np.asarray(Image.open(sys.argv[1]).convert("RGB"))
source = (source_rgba[..., 3] > 128).astype(np.uint8)
model = (np.max(np.abs(render.astype(np.int16)-np.array([210,225,202])),axis=2)>28).astype(np.uint8)
sy,sx=np.where(source)
my,mx=np.where(model)
sb=(sx.min(),sy.min(),sx.max()+1,sy.max()+1)
mb=(mx.min(),my.min(),mx.max()+1,my.max()+1)
target=np.zeros(model.shape,np.uint8)
target[mb[1]:mb[3],mb[0]:mb[2]]=cv2.resize(
    source[sb[1]:sb[3],sb[0]:sb[2]],(mb[2]-mb[0],mb[3]-mb[1]),
    interpolation=cv2.INTER_NEAREST)
only_model=(model>0)&(target==0)
only_target=(target>0)&(model==0)
iou=int(np.logical_and(model,target).sum())/int(np.logical_or(model,target).sum())
marked=render.copy()
marked[only_model]=[235,54,76]
marked[only_target]=[26,205,222]
width,height=render.shape[1],render.shape[0]
out=Image.new("RGB",(width*2,height+55),"#d2e1ca")
out.paste(Image.fromarray(render),(0,35))
out.paste(Image.fromarray(marked),(width,35))
d=ImageDraw.Draw(out)
d.text((10,10),"Current front render",fill="black")
d.text((width+10,10),f"Outline overlap: {iou:.3f}",fill="black")
d.text((width+10,height+40),"Red: model extra. Cyan: reference missing.",fill="black")
out.save(sys.argv[2])
print(f"Outline overlap {iou:.3f}; model-only {only_model.sum():,} pixels; reference-only {only_target.sum():,} pixels")
