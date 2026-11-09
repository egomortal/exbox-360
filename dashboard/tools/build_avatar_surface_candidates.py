"""Bounded facial surface reconstruction with feature and silhouette constraints."""
from pathlib import Path
import sys,json,subprocess,hashlib
import numpy as np
from scipy.ndimage import gaussian_filter,distance_transform_edt,map_coordinates
from PIL import Image,ImageDraw
import trimesh
r=Path(__file__).resolve().parents[1];out=r/'private/avatar/manual-geometry/surface-fairing';out.mkdir(exist_ok=True)
sys.path[:0]=[str(r/'tools'),str(r/'private/avatar/multiview')]
from import_image_model import read_obj
from project_multiview_colors import depth_map
names=['b1-mv-profile','b2-mv','b3-mv','cereza-mv']
landmarks=[[(549,282,8,10),(540,301,16,5)],[(520,202,7,8),(519,219,15,5)],[(536,245,7,8),(533,255,15,5)],[(517,258,7,8),(515,271,16,5)]]
allreports=[]
for index,name in enumerate(names):
 base=r/'private/avatar/multiview/cleaned'/name;w=out/name;w.mkdir(exist_ok=True)
 sourcepath=base/'model.obj' if index else r/'private/avatar/manual-geometry/b1/b1-rebuilt.obj';texpath=base/'surface.png' if index else r/'private/avatar/manual-geometry/b1/b1-rebuilt.png'
 s,uv,f=read_obj(sourcepath);xyz=s[:,[1,2,0]].astype(float);h=np.ptp(xyz[:,1]);scale=839.68/h;cy=(xyz[:,1].max()+xyz[:,1].min())/2
 controls=json.loads((base/'controls.json').read_text());boundary=controls['face_boundary']
 if index==0:boundary=[[500,231],[516,222],[538,235],[554,254],[591,265],[597,284],[571,313],[541,329],[516,315],[492,290],[485,262]]
 saved=w/'controls-and-validation.json'
 if saved.exists():
  previous=json.loads(saved.read_text());boundary=previous['face_boundary'];landmarks[index]=previous['protected_landmarks']
 sigma=float(previous.get('sigma_pixels',4.5)) if saved.exists() else 4.5
 maskim=Image.new('L',(1024,1024));ImageDraw.Draw(maskim).polygon([tuple(p) for p in boundary],fill=255);mask=np.array(maskim)>0
 depth=depth_map(trimesh.Trimesh(vertices=xyz,faces=f,process=False),-xyz[:,0],xyz[:,1],xyz[:,2]);valid=mask&(depth>0)
 # A spatially bounded heightfield fairing removes reconstructed frame ridges.
 smooth=gaussian_filter(np.where(valid,depth-2,0),sigma)/np.maximum(gaussian_filter(valid.astype(float),sigma),1e-8)
 weight=np.clip(distance_transform_edt(mask)/10,0,1);yy,xx=np.indices(mask.shape)
 for x,y,rx,ry in landmarks[index]:weight*=1-.98*np.exp(-.5*((xx-x)/rx)**2-.5*((yy-y)/ry)**2)
 u=512-xyz[:,0]*scale;v=512-(xyz[:,1]-cy)*scale
 ix=np.clip(np.rint(u).astype(int),0,1023);iy=np.clip(np.rint(v).astype(int),0,1023)
 front=depth[iy,ix]-2;visible=(depth[iy,ix]>0)&(xyz[:,2]>=front-.012)
 delta=(map_coordinates(smooth,[v,u],order=1,mode='nearest')-xyz[:,2])*weight[iy,ix]*visible
 delta=np.clip(delta,-.012*h,.012*h)
 # Weld UV seams for equal displacement, then locally back off orientation failures.
 _,inverse=np.unique(np.round(xyz,6),axis=0,return_inverse=True);sums=np.bincount(inverse,weights=delta);counts=np.bincount(inverse);move=sums/counts
 original=np.cross(xyz[f[:,1]]-xyz[f[:,0]],xyz[f[:,2]]-xyz[f[:,0]])
 for attempt in range(100):
  new=xyz.copy();new[:,2]+=move[inverse];new=new.astype(np.float32).astype(float)
  cross=np.cross(new[f[:,1]]-new[f[:,0]],new[f[:,2]]-new[f[:,0]]);bad=(cross*original).sum(1)<0
  if not bad.any():break
  move[np.unique(inverse[f[bad]])]*=.7
 else:raise RuntimeError('Orientation repair failed')
 collapsed=(np.linalg.norm(original,axis=1)>1e-10)&(np.linalg.norm(cross,axis=1)<1e-10)
 if collapsed.any():raise RuntimeError('New collapsed triangle')
 with (w/'model.obj').open('w') as file:
  for p in new[:,[2,0,1]]:file.write('v %.10g %.10g %.10g\n'%tuple(p))
  for p in uv:file.write('vt %.10g %.10g\n'%tuple(p))
  for tri in f+1:file.write('f '+' '.join(f'{i}/{i}' for i in tri)+'\n')
 report={'name':name,'status':'candidate; review required','method':'feature-constrained front-visible depth fairing','source':str(sourcepath),'texture':str(texpath),'texture_sha256':hashlib.sha256(texpath.read_bytes()).hexdigest(),'texture_modified':False,'face_boundary':boundary,'protected_landmarks':landmarks[index],'sigma_pixels':sigma,'vertices_changed':int((np.linalg.norm(new-xyz,axis=1)>1e-7).sum()),'max_displacement':float(abs(new[:,2]-xyz[:,2]).max()),'front_xy_identical':bool(np.array_equal(new[:,:2],xyz[:,:2])),'opposed_normals':int(bad.sum()),'collapsed_triangles':int(collapsed.sum()),'orientation_iterations':attempt}
 (w/'controls-and-validation.json').write_text(json.dumps(report,indent=2));allreports.append(report)
 def run(*args):subprocess.run(list(map(str,args)),check=True)
 run(sys.executable,r/'private/avatar/multiview/render_projection_guides.py',w/'model.obj',texpath,w/'guide.png','--tools',r/'tools')
 run(sys.executable,r/'tools/import_image_model.py',r/'private/avatar/kit360.base.nxak',w/'model.obj',texpath,w/'candidate.nxak','--label',name+' surface candidate','--height',1.15 if index==3 else 1.73,'--mirror-x')
 frames=w/'frames';frames.mkdir(exist_ok=True);run('/tmp/render_image_model_mv',w/'candidate.nxak',frames,24,1)
 run(sys.executable,r/'tools/make_avatar_sweep.py',frames,w/'sweep.png','--metrics',w/'metrics.json')
 print(json.dumps(report),flush=True)
(out/'validation.json').write_text(json.dumps(allreports,indent=2))
