"""Rasterize recovered XUI figure materials for the native Blades controls.

Text/image presenters stay native. This is a bounded 2D material importer,
not an implementation of the dashboard's complete XUI runtime.
"""
from pathlib import Path
import hashlib, math, xml.etree.ElementTree as ET
import numpy as np
from PIL import Image, ImageDraw
from compile_xui_scene import flattened,original_paths,path_key

def properties(node):
    return {p.tag:p.text for p in node.findall('Properties/*') if len(p)==0}

def vector(value,default):
    return np.array([float(v) for v in value.split(',')]) if value else np.array(default,dtype=float)

def rgba(value):
    value=int(value or '0xff0f0f80',16)
    return np.array([(value>>16)&255,(value>>8)&255,value&255,value>>24],dtype=np.float32)

def interpolate(a,b,t,name=''):
    if a is None or b is None:return a
    if a.startswith('0x') and b.startswith('0x'):
        color=np.floor(rgba(a)*(1-t)+rgba(b)*t+.5)
        return '0x%02x%02x%02x%02x'%(int(color[3]),int(color[0]),int(color[1]),int(color[2]))
    if name in {'Show','Enabled','BlendMode','Fill.Gradient.NumStops','Fill.TextureFileName','Visual','File'}:return a
    try:
        av=vector(a,[]);bv=vector(b,[])
        if name=='Rotation' and np.dot(av,bv)<0:bv=-bv
        value=av*(1-t)+bv*t
        if name=='Rotation' and np.linalg.norm(value)>1e-12:value/=np.linalg.norm(value)
        return ','.join(str(v) for v in value)
    except ValueError:return a

def timeline_overrides(root,frame):
    result={}
    for timeline in root.findall('Timelines/Timeline'):
        names=[p.text+('['+p.get('index')+']' if p.get('index') is not None else '') for p in timeline.findall('TimelineProp')]
        keys=timeline.findall('KeyFrame')
        if not keys:continue
        left=keys[0];right=left
        for key in keys:
            time=float(key.findtext('Time'))
            if time<=frame:left=key;right=key
            else:right=key;break
        start=float(left.findtext('Time'));end=float(right.findtext('Time'))
        t=np.clip((frame-start)/(end-start),0,1) if end>start else 0
        kind=int(left.findtext('Interpolation') or 0)
        if any(k.find('OriginalEaseCoefficients') is not None or (k.findtext('OriginalFlag') is not None and int(k.findtext('OriginalFlag')) not in (0,1,2)) for k in keys):raise ValueError('Unverified source curve extension')
        if kind==1:t=0
        elif kind==2:
            signed=lambda v:float(v)-256 if float(v)>127 else float(v)
            ein=signed(left.findtext('EaseIn') or '0');eout=signed(left.findtext('EaseOut') or '0')
            if float(left.findtext('EaseScale') or 50)!=50:raise ValueError('Unimplemented non-default EaseScale')
            u=1-t;t=3*u*u*t*(1-ein/100)/3+3*u*t*t*(1-(1-eout/100)/3)+t*t*t
        elif kind!=0:raise ValueError('Unsupported interpolation '+str(kind))
        values=[interpolate(a.text,b.text,t,name.split('[')[0]) for a,b,name in zip(left.findall('Prop'),right.findall('Prop'),names)]
        if len(values)!=len(names):raise ValueError('Timeline property count mismatch')
        result.setdefault(timeline.findtext('Id'),{}).update(zip(names,values))
    return result

def transform(props):
    pos=vector(props.get('Position'),[0,0,0]);scale=vector(props.get('Scale'),[1,1,1])
    pivot=vector(props.get('Pivot'),[0,0,0]);q=vector(props.get('Rotation'),[0,0,0,1])
    q=q/np.linalg.norm(q) if np.linalg.norm(q)>0 else np.array([0,0,0,1])
    qx,qy,qz,qw=q
    matrix=np.array([[1-2*(qy*qy+qz*qz),2*(qx*qy-qz*qw)],
                     [2*(qx*qy+qz*qw),1-2*(qx*qx+qz*qz)]])@np.diag(scale[:2])
    out=np.eye(3);out[:2,:2]=matrix;out[:2,2]=pos[:2]+pivot[:2]-matrix@pivot[:2]
    return out

def bezier_points(value):
    return np.array(flattened(value)[1])

class Raster:
    def __init__(self,size,paths,supersample=3):
        self.size=size;self.ss=supersample
        self.paths=paths
        self.image=Image.new('RGBA',(size[0]*supersample,size[1]*supersample))
        self.figures=0

    def figure(self,node,props,matrix,opacity):
        points=bezier_points(props['Points'])
        raw,_=flattened(props['Points']);key=path_key(raw)
        if key not in self.paths:raise ValueError('Original CUST path bounds missing for '+props.get('Id',''))
        path_width,path_height=self.paths[key]
        if path_width<=0 or path_height<=0:return
        width=float(props.get('Width','60'));height=float(props.get('Height','30'))
        if width<=0 or height<=0:return
        if float(node.findtext('Properties/Stroke/Properties/StrokeWidth') or 0):raise ValueError('Stroke requires the native stroke renderer')
        points=points*np.array([width/path_width,height/path_height])
        projected=np.c_[points,np.ones(len(points))]@matrix.T
        projected=projected[:,:2]*self.ss
        bounds=[max(0,int(np.floor(projected[:,0].min()))-1),max(0,int(np.floor(projected[:,1].min()))-1),
                min(self.image.width,int(np.ceil(projected[:,0].max()))+2),min(self.image.height,int(np.ceil(projected[:,1].max()))+2)]
        x0,y0,x1,y1=bounds
        if x1<=x0 or y1<=y0:return
        mask=Image.new('L',(x1-x0,y1-y0));ImageDraw.Draw(mask).polygon([tuple(p-[x0,y0]) for p in projected],fill=255)
        yy,xx=np.mgrid[y0:y1,x0:x1].astype(np.float32)
        inv=np.linalg.inv(matrix)
        u=(inv[0,0]*(xx+.5)/self.ss+inv[0,1]*(yy+.5)/self.ss+inv[0,2])/width
        v=(inv[1,0]*(xx+.5)/self.ss+inv[1,1]*(yy+.5)/self.ss+inv[1,2])/height
        fill=node.find('Properties/Fill/Properties')
        if fill is None:fill=ET.Element('Properties')
        gradient=fill.find('Gradient/Properties')
        def value(name,default=None):return props.get('Fill.'+name,fill.findtext(name,default))
        if value('FillType')=='0':return
        if value('TextureFileName'):raise ValueError('Texture fill requires original bitmap binding in the native renderer')
        if gradient is None:
            color=np.broadcast_to(rgba(value('FillColor')),(y1-y0,x1-x0,4)).copy()
        else:
            positions=gradient.findall('StopPos');source_colors=gradient.findall('StopColor')
            count=int(float(props.get('Fill.Gradient.NumStops',gradient.findtext('NumStops') or len(positions))))
            stops=[float(props.get('Fill.Gradient.StopPos['+p.get('index',str(i))+']',p.text)) for i,p in enumerate(positions[:count])]
            colors=np.array([rgba(props.get('Fill.Gradient.StopColor['+p.get('index',str(i))+']',p.text)) for i,p in enumerate(source_colors[:count])])
            if not stops or len(stops)!=len(colors) or any(a>b for a,b in zip(stops,stops[1:])):raise ValueError('Invalid animated gradient stops')
            trans=vector(value('Translation'),[0,0,0]);scale=vector(value('Scale'),[1,1,1])
            angle=math.radians(float(value('Rotation') or 0))
            gu=(u-.5)*scale[0]*math.cos(angle)-(v-.5)*scale[1]*math.sin(angle)+trans[0]
            gv=(u-.5)*scale[0]*math.sin(angle)+(v-.5)*scale[1]*math.cos(angle)+trans[1]
            phase=2*np.sqrt(gu*gu+gv*gv) if gradient.findtext('Radial')=='true' else gu+.5
            color=np.stack([np.interp(phase,stops,colors[:,channel]) for channel in range(4)],axis=-1)
        color[:,:,3]*=np.asarray(mask)/255*opacity
        self.image.alpha_composite(Image.fromarray(np.uint8(np.floor(np.clip(color,0,255)+.5)),'RGBA'),(x0,y0))
        self.figures+=1

    def node(self,node,matrix=None,opacity=1,overrides=None,blend=1):
        props=properties(node);props.update((overrides or {}).get(props.get('Id'),{}))
        if props.get('Show')=='false':return
        opacity*=float(props.get('Opacity') or 1)
        if opacity<=0:return
        blend=int(float(props.get('BlendMode') or blend))
        if blend not in (0,1):raise ValueError('Destination-dependent blend '+str(blend)+' cannot be flattened into a reusable normal-alpha PNG')
        matrix=(np.eye(3) if matrix is None else matrix)@transform(props)
        if node.tag=='XuiFigure' and props.get('Points'):
            self.figure(node,props,matrix,opacity)
        for child in node:
            if child.tag in ['XuiFigure','XuiGroup']:self.node(child,matrix,opacity,overrides,blend)

    def finish(self):
        return self.image.resize(self.size,Image.Resampling.LANCZOS)

def control(source,identity,frame,size,padding=18,original_xur=None):
    if original_xur is None:raise ValueError('Original XUR is required; never infer vector bounds from visible extrema')
    root=ET.parse(source).getroot()
    node=next(n for n in root.findall('XuiVisual') if n.findtext('Properties/Id')==identity)
    props=properties(node);w=float(props['Width']);h=float(props['Height'])
    matrix=np.array([[size[0]/w,0,padding],[0,size[1]/h,padding],[0,0,1]])@transform(props)
    raster=Raster((size[0]+2*padding,size[1]+2*padding),original_paths(original_xur))
    overrides=timeline_overrides(node,frame)
    for child in node:
        if child.tag in ['XuiFigure','XuiGroup']:raster.node(child,matrix,1,overrides)
    return raster.finish(),raster.figures

def shine(source,identity,size,original_xur=None):
    if original_xur is None:raise ValueError('Original XUR is required')
    root=ET.parse(source).getroot()
    visual=next(n for n in root.findall('XuiVisual') if n.findtext('Properties/Id')==identity)
    node=next(n for n in visual.findall('XuiFigure') if n.findtext('Properties/Id') in ['shineLoop','shine_loop'])
    props=properties(node);raster=Raster(size,original_paths(original_xur))
    sx=size[0]/float(props['Width']);sy=size[1]/float(props['Height'])
    overrides={props['Id']:{'Position':'0,0,0','Opacity':'1','Show':'true'}}
    raster.node(node,np.diag([sx,sy,1]),1,overrides)
    return raster.finish()

if __name__=='__main__':
    import argparse,json
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path);parser.add_argument('--original-xur',type=Path,required=True);parser.add_argument('--native-bundle',action='store_true')
    args=parser.parse_args();args.destination.mkdir(parents=True,exist_ok=True)
    records=[]
    cases=[('row-normal','btn_1line_icon',14,(584,74)),('row-focus','btn_1line_icon',27,(584,74)),('profile','btn_Gamercard',10,(584,208))]
    if args.native_bundle:
        for active in range(5):
            for early in ([False] if active==0 else [False,True]):
                width=1199-6*(active-1)-(0 if early else 50)
                shine(args.source,'btn_tray',(round(335*width/712.061646),64),args.original_xur).save(args.destination/f'tray-{4 if early else 5}-{active}-shine.png')
                for state,frame in [('normal',10),('focus',19)]:
                    cases.append((f'tray-{4 if early else 5}-{active}-{state}','btn_tray',frame,(width,105)))
        for name,identity,size in [('row-shine','btn_1line_icon',(584,66)),('tray-shine','btn_tray',(560,64))]:
            shine(args.source,identity,size,args.original_xur).save(args.destination/(name+'.png'))
    else:
        cases += [('tray-normal','btn_tray',10,(1190,105)),('tray-focus','btn_tray',19,(1190,105))]
    for name,identity,frame,size in cases:
        image,count=control(args.source,identity,frame,size,original_xur=args.original_xur);image.save(args.destination/(name+'.png'))
        records.append(dict(file=name+'.png',visual=identity,frame=frame,figures=count,size=image.size))
    original_values={}
    for visual in ET.parse(args.source).getroot().findall('XuiVisual'):
        identity=visual.findtext('Properties/Id')
        if identity not in ['btn_1line_icon','btn_Gamercard','btn_tray']:continue
        layers=[]
        for index,node in enumerate(visual.findall('.//XuiFigure')):
            fill=node.find('Properties/Fill/Properties')
            if fill is None:continue
            gradient=fill.find('Gradient/Properties')
            layers.append(dict(layer=index,id=node.findtext('Properties/Id'),
                opacity=node.findtext('Properties/Opacity'),fill=fill.findtext('FillColor'),
                fill_transform={k:fill.findtext(k) for k in ['Translation','Scale','Rotation']},
                gradient=None if gradient is None else dict(radial=gradient.findtext('Radial')=='true',
                    colors=[n.text for n in gradient.findall('StopColor')],positions=[float(n.text) for n in gradient.findall('StopPos')])) )
        original_values[identity]=layers
    (args.destination/'xui-materials.json').write_text(json.dumps(dict(source=str(args.source),provenance='derived conversion of original XUI geometry; not untouched original raster artwork',original_xur=str(args.original_xur),original_xur_sha256=hashlib.sha256(args.original_xur.read_bytes()).hexdigest(),unsupported='Destination-dependent blends, texture fills, open strokes rejected; text/image presenters remain native',supersampling=3,
        source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),records=records,
        original_material_values=original_values),indent=2)+'\n')
    print(json.dumps(records,indent=2))
