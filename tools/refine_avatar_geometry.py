#!/usr/bin/env python3
"""Smooth reconstructed head depth while preserving front coordinates and UVs."""
import argparse,json,sys
from pathlib import Path
import numpy as np
from scipy.sparse import coo_matrix
from import_image_model import read_obj

def refine(source, faces, head_start, iterations, limit, controls=None):
    xyz=source[:,[1,2,0]].astype(np.float64)
    unique,inverse=np.unique(np.round(xyz,6),axis=0,return_inverse=True)
    welded_faces=inverse[faces]
    edges=np.concatenate([welded_faces[:,[0,1]],welded_faces[:,[1,2]],welded_faces[:,[2,0]]])
    edges=np.unique(np.concatenate([edges,edges[:,::-1]]),axis=0)
    adjacency=coo_matrix((np.ones(len(edges)),(edges[:,0],edges[:,1])),shape=(len(unique),len(unique))).tocsr()
    degree=np.maximum(np.asarray(adjacency.sum(axis=1)),1)
    height=np.ptp(unique[:,1]);relative=(unique[:,1]-unique[:,1].min())/height
    selection=np.clip((relative-head_start)/.035,0,1)[:,None]
    if controls is not None:
        from PIL import Image,ImageDraw
        from scipy.ndimage import distance_transform_edt
        import trimesh
        sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'private/avatar/multiview'))
        from project_multiview_colors import depth_map
        mask=Image.new('L',(1024,1024),0)
        ImageDraw.Draw(mask).polygon([tuple(p) for p in controls['face_boundary']],fill=255)
        softness=np.clip(distance_transform_edt(np.asarray(mask)>0)/8,0,1)
        scale=1024*.82/height;cy=(unique[:,1].max()+unique[:,1].min())/2
        ix=np.clip(np.rint(512-unique[:,0]*scale).astype(int),0,1023)
        iy=np.clip(np.rint(512-(unique[:,1]-cy)*scale).astype(int),0,1023)
        mesh=trimesh.Trimesh(vertices=xyz,faces=faces,process=False)
        front=depth_map(mesh,-xyz[:,0],xyz[:,1],xyz[:,2])
        visible=(front[iy,ix]>0)&(unique[:,2]+2>=front[iy,ix]-.04)
        selection*=softness[iy,ix,None]*visible[:,None]
    current=unique.copy()
    for _ in range(iterations):
        for factor in (.5,-.53):
            current+=((adjacency@current)/degree-current)*factor*selection
        delta=current-unique
        current=unique+delta*np.minimum(1,height*limit/np.maximum(np.linalg.norm(delta,axis=1,keepdims=True),1e-12))
    displacement=current[:,2]-unique[:,2]
    original_cross=np.cross(xyz[faces[:,1]]-xyz[faces[:,0]],xyz[faces[:,2]]-xyz[faces[:,0]])
    adjusted=np.zeros(len(unique),bool)
    for repair in range(100):
        result=xyz.copy();result[:,2]+=displacement[inverse]
        # Match the importer's float32 coordinates before checking orientation.
        result=result.astype(np.float32).astype(np.float64)
        cross=np.cross(result[faces[:,1]]-result[faces[:,0]],result[faces[:,2]]-result[faces[:,0]])
        bad=(original_cross*cross).sum(axis=1)<0
        if not bad.any():break
        affected=np.unique(welded_faces[bad]);adjusted[affected]=True
        displacement[affected]*=.5
        displacement[np.abs(displacement)<1e-8]=0
    else:raise ValueError('Unable to preserve face orientation; no output written')
    collapsed=(np.linalg.norm(original_cross,axis=1)>1e-10)&(np.linalg.norm(cross,axis=1)<1e-10)
    if collapsed.any():raise ValueError('Refinement collapsed triangles; no output written')
    report={'method':'welded Taubin head depth smoothing','iterations':iterations,'head_start':head_start,'displacement_limit_fraction':limit,'front_xy_unchanged':bool(np.array_equal(result[:,:2],xyz[:,:2])),'vertices_moved':int((np.linalg.norm(result-xyz,axis=1)>1e-7).sum()),'max_displacement':float(np.max(np.linalg.norm(result-xyz,axis=1))),'orientation_backoff_vertices':int(adjusted.sum()),'opposed_face_normals':int(bad.sum()),'new_collapsed_triangles':int(collapsed.sum())}
    return result[:,[2,0,1]],report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--head-start',type=float,required=True)
    parser.add_argument('--controls',type=Path,help='optional front face boundary to protect ears and hair')
    parser.add_argument('--iterations',type=int,default=35)
    parser.add_argument('--limit',type=float,default=.009)
    args=parser.parse_args()
    if args.input.resolve()==args.output.resolve():parser.error('Input and output must differ')
    if not 0<args.head_start<1 or not 1<=args.iterations<=200 or not 0<args.limit<=.02:parser.error('Invalid refinement bounds')
    source,uv,faces=read_obj(args.input);result,report=refine(source,faces,args.head_start,args.iterations,args.limit,json.loads(args.controls.read_text()) if args.controls else None)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('w') as output:
        for v in result:output.write('v '+' '.join(format(float(x),'.10g') for x in v)+'\n')
        for t in uv:output.write('vt '+' '.join(format(float(x),'.10g') for x in t)+'\n')
        for f in faces+1:output.write('f '+' '.join(f'{i}/{i}' for i in f)+'\n')
    checked,checked_uv,checked_faces=read_obj(args.output)
    if not np.array_equal(checked,result.astype(np.float32)) or not np.array_equal(checked_uv,uv) or not np.array_equal(checked_faces,faces):raise ValueError('OBJ round-trip validation failed')
    report['face_controls']=str(args.controls) if args.controls else None;report['source']=str(args.input);report['output']=str(args.output)
    args.output.with_suffix('.geometry.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)
if __name__=='__main__':main()