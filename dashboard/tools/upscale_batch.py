# SPDX-License-Identifier: GPL-3.0-or-later
"""Local image-copy step for the setup window; originals remain untouched."""
from pathlib import Path
import argparse,json,os
from upscale import upscale,UpscaleError
EXT={'.png','.jpg','.jpeg','.bmp','.webp','.tif','.tiff'}
def images(paths):
 found=[];visited=0
 for value in paths:
  root=Path(value).expanduser().resolve(strict=True)
  if root.is_file():candidates=[root]
  else:
   candidates=[]
   for folder,dirs,files in os.walk(root,followlinks=False):
    dirs[:]=sorted(d for d in dirs if not (Path(folder)/d).is_symlink())
    for name in sorted(files):
     visited+=1
     if visited>4096:raise UpscaleError('This folder is too large; select a smaller image folder.')
     candidate=Path(folder)/name
     if candidate.suffix.lower() in EXT:
      if candidate.is_symlink():raise UpscaleError('Use image files directly, not symlinks.')
      candidates.append(candidate)
  for candidate in candidates:
   if candidate.suffix.lower() not in EXT:raise UpscaleError('Upscale accepts usable images, not archives or console packages.')
   if candidate not in found:found.append(candidate)
   if len(found)>64:raise UpscaleError('Choose at most64 images in one local operation.')
 if not found:raise UpscaleError('No supported images found in this selection.')
 return found

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('paths',nargs='+');parser.add_argument('--data-root',required=True);parser.add_argument('--runtime-dir',required=True);parser.add_argument('--kind',choices=['ui','text','gradient','photo'],default='ui');parser.add_argument('--scale',type=int,choices=[2,4],default=2);parser.add_argument('--method',choices=['auto','classic','ai'],default='auto');args=parser.parse_args();rows=[]
 try:
  for path in images(args.paths):
   try:
    result=upscale(path,args.data_root,args.kind,args.scale,args.method,args.runtime_dir);rows.append({'status':'recognized','source':str(path),'kind':'upscaled image','output':result['output'],'result':result})
   except (OSError,ValueError) as e:rows.append({'status':'failed','source':str(path),'reason':str(e)})
 except (OSError,ValueError) as e:print(json.dumps({'status':'error','message':str(e),'files':rows}));return 2
 ok=all(r['status']=='recognized' for r in rows);print(json.dumps({'status':'Upscaled locally; originals retained' if ok else 'Some images need attention','files':rows},indent=2));return 0 if ok else 2
if __name__=='__main__':raise SystemExit(main())
