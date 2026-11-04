# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic local upscaling of usable user images; no extraction or network."""
from pathlib import Path
import argparse,hashlib,io,json,os,platform,subprocess,tempfile,time,warnings
os.environ['OMP_NUM_THREADS']='1';os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['MKL_NUM_THREADS']='1'
import numpy as np
import PIL
from PIL import Image,ImageOps
ROOT=Path(__file__).resolve().parent
MODEL={'generic-x4.bin':'bfdfaebf0b26be9442faa30b8cec617fe04a808ae2aa3616ece85b92d81ae0ce','generic-x4.param':'ff5d1e61da61a8e581c9262dc41cc7a446d3866e49f11706d637ec1c632aedc7'}
MAX_INPUT=4*1024*1024;MAX_OUTPUT=16*1024*1024;TILE=128;HALO=34
Image.MAX_IMAGE_PIXELS=MAX_INPUT
class UpscaleError(ValueError):pass
def digest(data):return hashlib.sha256(data).hexdigest()
def verify_runtime(runtime=ROOT,ai=True):
 runtime=Path(runtime).resolve(strict=True)
 try:r=json.loads((runtime/'runtime-receipt.json').read_text())
 except (OSError,ValueError) as e:raise UpscaleError('Build and pin the local upscaler runtime first.') from e
 if r.get('schema')!=1 or r.get('profile')!='ncnn-cpu-fp32-singlethread-v1':raise UpscaleError('Unrecognized runtime profile.')
 if r.get('pillow')!=PIL.__version__ or r.get('numpy')!=np.__version__ or r.get('architecture')!=platform.machine() or r.get('python')!=platform.python_version():raise UpscaleError('Runtime versions differ from the pinned profile; rebuild and validate this local profile.')
 if ai:
  for name,h in MODEL.items():
   if digest((runtime/'model'/name).read_bytes())!=h or r.get('models',{}).get(name)!=h:raise UpscaleError('Model integrity check failed: '+name)
  if digest((runtime/'cpu-infer').read_bytes())!=r.get('adapter_sha256'):raise UpscaleError('Source-built CPU adapter integrity check failed.')
 return r

def read_image(path,scale):
 path=Path(path).expanduser().resolve(strict=True)
 if not path.is_file() or path.stat().st_size>64*1024*1024:raise UpscaleError('Choose a regular image file no larger than 64 MiB.')
 data=path.read_bytes()
 try:
  with warnings.catch_warnings():
   warnings.simplefilter('error',Image.DecompressionBombWarning)
   with Image.open(io.BytesIO(data)) as image:
    if image.format not in {'PNG','JPEG','BMP','WEBP','TIFF'} or getattr(image,'n_frames',1)!=1:raise UpscaleError('Choose a single-frame PNG, JPEG, BMP, WebP or TIFF image.')
    w,h=image.size
    if w<1 or h<1 or w*h>MAX_INPUT or w*h*scale*scale>MAX_OUTPUT or max(w,h)*scale>16384:raise UpscaleError('Image is too large for this bounded profile (4M input / 16M output pixels).')
    if image.mode in {'I','F','I;16','I;16B','I;16L'}:raise UpscaleError('Use an 8-bit image; high-bit-depth conversion is not part of this profile.')
    image.load();out=ImageOps.exif_transpose(image).convert('RGBA')
 except (OSError,Image.DecompressionBombError,Image.DecompressionBombWarning) as e:raise UpscaleError('This image is damaged or exceeds the pixel limit.') from e
 return path,data,out

def classic(image,scale):
 """Premultiplied Lanczos, clamped to visible local source-color bounds."""
 a=np.array(image.convert('RGBA'),dtype=np.float32);alpha=a[:,:,3]/255.;rgb=a[:,:,:3];h,w=alpha.shape;size=(w*scale,h*scale)
 def resize(plane,filter=Image.Resampling.LANCZOS):return np.array(Image.fromarray(plane.astype(np.float32),'F').resize(size,filter),dtype=np.float32)
 out_alpha=np.clip(resize(alpha),0,1);premult=np.stack([resize(rgb[:,:,c]*alpha) for c in range(3)],axis=2)
 out=np.divide(premult,out_alpha[:,:,None],out=np.zeros_like(premult),where=out_alpha[:,:,None]>1e-6)
 padded=np.pad(rgb,((1,1),(1,1),(0,0)),mode='edge');valid=np.pad(alpha>0,((1,1),(1,1)),mode='edge');lo=np.full_like(rgb,255);hi=np.zeros_like(rgb)
 for y in range(3):
  for x in range(3):
   block=padded[y:y+h,x:x+w];mask=valid[y:y+h,x:x+w,None];lo=np.minimum(lo,np.where(mask,block,255));hi=np.maximum(hi,np.where(mask,block,0))
 for c in range(3):
  lower=resize(lo[:,:,c],Image.Resampling.NEAREST);upper=resize(hi[:,:,c],Image.Resampling.NEAREST);out[:,:,c]=np.clip(out[:,:,c],np.minimum(lower,upper),upper)
 out[out_alpha<1/510]=0
 rgba=np.concatenate([np.clip(out,0,255),out_alpha[:,:,None]*255],axis=2)
 return Image.fromarray(np.floor(rgba+0.5).astype(np.uint8),'RGBA')

def infer_tile(rgb,runtime,work,timeout):
 h,w,_=rgb.shape;src=work/'input.rgb';dst=work/'output.rgb';src.write_bytes(rgb.tobytes())
 command=[str(runtime/'cpu-infer'),str(runtime/'model/generic-x4.param'),str(runtime/'model/generic-x4.bin'),str(src),str(dst),str(w),str(h)]
 try:r=subprocess.run(command,capture_output=True,text=True,timeout=timeout,env={**os.environ,'OMP_NUM_THREADS':'1'})
 except subprocess.TimeoutExpired as e:raise UpscaleError('Upscaling exceeded the time limit; try a smaller image or the classic method.') from e
 if r.returncode or not dst.is_file() or dst.stat().st_size!=w*h*16*3:raise UpscaleError('CPU inference failed; verify the pinned local runtime.')
 return np.frombuffer(dst.read_bytes(),dtype=np.uint8).reshape(h*4,w*4,3)

def ai(image,scale,runtime=ROOT,timeout=600,tile=TILE):
 rgba=np.array(image.convert('RGBA'));h,w,_=rgba.shape
 if np.any(rgba[:,:,3]!=255):raise UpscaleError('AI photo mode requires opaque input. Use classic for transparency; no alpha is invented.')
 if tile<1 or tile+2*HALO>512:raise UpscaleError('Invalid tile size.')
 result=np.empty((h*4,w*4,3),np.uint8);deadline=time.monotonic()+timeout
 with tempfile.TemporaryDirectory(prefix='exbox-upscale-') as tmp:
  work=Path(tmp)
  for y in range(0,h,tile):
   for x in range(0,w,tile):
    x1=min(w,x+tile);y1=min(h,y+tile);left=max(0,x-HALO);top=max(0,y-HALO);right=min(w,x1+HALO);bottom=min(h,y1+HALO)
    remaining=deadline-time.monotonic()
    if remaining<=0:raise UpscaleError('Upscaling exceeded the total time limit.')
    out=infer_tile(rgba[top:bottom,left:right,:3].copy(),Path(runtime),work,min(remaining,120))
    result[y*4:y1*4,x*4:x1*4]=out[(y-top)*4:(y1-top)*4,(x-left)*4:(x1-left)*4]
 out=Image.fromarray(result,'RGB')
 return out if scale==4 else out.resize((w*scale,h*scale),Image.Resampling.LANCZOS)

def source_roots():
 return [p for p in [ROOT,*ROOT.parents] if (p/'setup.py').is_file() or ((p/'main.cpp').is_file() and (p/'dash_model.hpp').is_file())]
def destination(input_path,data_root,output_name):
 data=Path(data_root).expanduser().resolve()
 if (data/'upscaled').is_symlink():raise UpscaleError('The upscaled output folder cannot be a symlink.')
 root=(data/'upscaled').resolve()
 if any(data==p or data.is_relative_to(p) or root==p or root.is_relative_to(p) for p in source_roots()):raise UpscaleError('Choose a user data folder outside the source checkout.')
 if not output_name or Path(output_name).name!=output_name or '/' in output_name or '\\' in output_name or ':' in output_name or not output_name.lower().endswith('.png'):raise UpscaleError('Output name must be a plain PNG filename.')
 target=root/output_name
 if target.is_symlink() or (target.exists() and target.resolve()==input_path) or target.resolve()==input_path:raise UpscaleError('Output cannot replace the original image or follow a symlink.')
 if not target.resolve().is_relative_to(root) or not root.is_relative_to(data):raise UpscaleError('Output escapes the chosen data folder.')
 return target

def upscale(input_path,data_root,kind='ui',scale=2,method='auto',runtime_dir=ROOT,output_name=None,timeout=600):
 if kind not in {'ui','text','gradient','photo'} or scale not in {2,4} or method not in {'auto','classic','ai'} or not 1<=timeout<=3600:raise UpscaleError('Invalid kind, scale, method or time limit.')
 selected='classic' if method=='auto' else method
 runtime=Path(runtime_dir).resolve();profile=verify_runtime(runtime,selected=='ai');source,raw,image=read_image(input_path,scale)
 target=destination(source,data_root,output_name or f'{source.stem}.{kind}.{scale}x.png')
 output=ai(image,scale,runtime,timeout) if selected=='ai' else classic(image,scale)
 buffer=io.BytesIO();output.save(buffer,format='PNG',compress_level=9,optimize=False);encoded=buffer.getvalue();target.parent.mkdir(parents=True,exist_ok=True)
 if target.exists():
  if target.read_bytes()!=encoded:raise UpscaleError('Output already exists with different bytes; choose another name.')
 else:
  fd,temp=tempfile.mkstemp(prefix='.upscale-',suffix='.tmp',dir=target.parent)
  try:
   with os.fdopen(fd,'wb') as f:f.write(encoded);f.flush();os.fsync(f.fileno())
   os.link(temp,target)
  finally:
   Path(temp).unlink(missing_ok=True)
 return {'schema':1,'input':str(source),'input_sha256':digest(raw),'output':str(target),'output_sha256':digest(encoded),'dimensions':list(output.size),'kind':kind,'method':selected,'scale':scale,'recipe':'exbox-upscale-v1','profile':profile['profile'],'model_sha256':MODEL['generic-x4.bin'] if selected=='ai' else None,'original_unchanged':digest(source.read_bytes())==digest(raw)}

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input');parser.add_argument('--data-root',required=True);parser.add_argument('--kind',choices=['ui','text','gradient','photo'],default='ui');parser.add_argument('--scale',type=int,choices=[2,4],default=2);parser.add_argument('--method',choices=['auto','classic','ai'],default='auto');parser.add_argument('--runtime-dir',type=Path,default=ROOT);parser.add_argument('--output-name');parser.add_argument('--timeout',type=int,default=600);args=parser.parse_args()
 try:result=upscale(args.input,args.data_root,args.kind,args.scale,args.method,args.runtime_dir,args.output_name,args.timeout)
 except (UpscaleError,OSError,ValueError) as e:print(json.dumps({'status':'error','message':str(e)}));return 2
 print(json.dumps({'status':'complete',**result},indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
