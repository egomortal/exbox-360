"""Compile owner XUI/XUR scenes into bounded native animation/geometry packs."""
from pathlib import Path
import argparse,copy,hashlib,json,math,struct
import xml.etree.ElementTree as ET

FIELDS=['Position','Scale','Rotation','Pivot','Width','Height','Opacity','Show','Anchor',
        'BlendMode','Visual','Fill.FillColor','Fill.Scale','Fill.Translation','Fill.Rotation',
        'Stroke.StrokeWidth','Stroke.StrokeColor','TextColor','DropShadowColor','Text',
        'PointSize','TextStyle','Fill.TextureFileName','Enabled','Fill.Gradient.StopPos',
        'Fill.Gradient.StopColor','Fill.Gradient.NumStops','File','Font','LineSpacingAdjust','TextScale','SizeMode']
COLOR={11,16,17,18,25};TOKEN={10,19,22,27,28};INTEGER={7,8,9,21,23,26,29,31}
def props(node):return {p.tag:p.text for p in node.findall('Properties/*') if len(p)==0}
def path_key(points):return tuple(struct.pack('>f',v) for row in points for v in row[:6])

def original_paths(file):
    data=file.read_bytes()
    if data[:4]!=b'XUIB':raise ValueError('Not an original XUR')
    at=data.find(b'CUST',20,256)
    if at<0:return {}
    start,size=struct.unpack_from('>II',data,at+4)
    if start+size>len(data):raise ValueError('Invalid CUST bounds')
    end=start+size;result={}
    while start<end:
        length,w,h,count=struct.unpack_from('>IffI',data,start)
        if length!=12+count*24 or start+4+length>end:raise ValueError('Invalid path record')
        points=[struct.unpack_from('>6f',data,start+16+i*24) for i in range(count)]
        result[path_key(points)]=(w,h);start+=length+4
    return result

def flattened(value):
    raw=value.strip(',').split(',');n=int(raw[0])
    single=lambda value:struct.unpack('>f',struct.pack('>f',float(value)))[0]
    p=[[single(v) for v in raw[1+i*7:7+i*7]] for i in range(n)]
    result=[]
    for i,a in enumerate(p):
        b=p[(i+1)%n]
        straight=a[4:6]==a[:2] and b[2:4]==b[:2]
        count=1 if straight else 32
        for s in range(count):
            t=s/count;u=1-t
            result.append(tuple(u*u*u*a[j]+3*u*u*t*a[4+j]+3*u*t*t*b[2+j]+t*t*t*b[j] for j in range(2)))
    return p,result

class Compiler:
    def __init__(self,sources):
        self.visuals={};self.paths={};self.source=[]
        for xml,xur in sources:
            self.source.append(dict(xml=str(xml),xml_sha256=hashlib.sha256(xml.read_bytes()).hexdigest(),
                xur=str(xur),xur_sha256=hashlib.sha256(xur.read_bytes()).hexdigest()))
            self.paths.update(original_paths(xur))
            for node in ET.parse(xml).getroot().findall('XuiVisual'):
                self.visuals[node.findtext('Properties/Id')]=node
        self.nodes=[];self.animations=[];self.tracks=[];self.keys=[];self.ranges=[];self.points=[];self.stops=[]
        self.strings=bytearray(b'\0');self.string_ids={'':0};self.pending=[];self.missing=set();self.unhandled=set();self.paths_recovered=0
    def string(self,value):
        value=value or ''
        if value not in self.string_ids:
            self.string_ids[value]=len(self.strings);self.strings.extend(value.encode('utf-8')+b'\0')
        return self.string_ids[value]
    def val(self,field,value):
        if value is None:return ([0.,0.,0.,0.],0)
        if field in TOKEN:return ([0.,0.,0.,0.],self.string(value))
        if field in COLOR:return ([0.,0.,0.,0.],int(value,16))
        if value in ('true','false'):return ([float(value=='true'),0.,0.,0.],int(value=='true'))
        try:
            v=list(map(float,value.split(',')));return ((v+[0.,0.,0.,0.])[:4],int(v[0]) if field in INTEGER else 0)
        except ValueError:raise ValueError(f'Unsupported value {FIELDS[field]}: {value}')
    def properties(self,element):
        values=[self.val(i,None) for i in range(len(FIELDS))]
        for i,v in {1:'1,1,1',2:'0,0,0,1',4:'60',5:'30',6:'1',7:'true',11:'0xff0f0f80',12:'1,1,1',17:'0xffffffff',20:'14',23:'true',30:'1'}.items():values[i]=self.val(i,v)
        for i,name in enumerate(FIELDS):
            path='Properties/'+'/Properties/'.join(name.split('.'))
            node=element.find(path)
            if node is not None and len(node)==0:values[i]=self.val(i,node.text or '')
        return values
    def add(self,element,parent=-1,design=(1120.,770.),branch=(-1,0),chain=()):
        prop=props(element);identity=prop.get('Id','');index=len(self.nodes)
        values=self.properties(element);w=values[4][0][0];h=values[5][0][0]
        kinds={'XuiFigure':1,'XuiText':2,'XuiImage':3,'XuiImagePresenter':4,'XuiTextPresenter':5,'XuiNineGrid':3}
        kind=kinds.get(element.tag,0)
        start=len(self.points);count=0;path_size=(w,h)
        if kind==1 and prop.get('Points'):
            raw,points=flattened(prop['Points']);key=path_key(raw)
            if key not in self.paths:raise ValueError(f'Original path bounds missing for {identity}')
            path_size=self.paths[key];self.paths_recovered+=1
            self.points.extend(points);count=len(points)
        stop_start=len(self.stops);g=element.find('Properties/Fill/Properties/Gradient/Properties')
        fill_type=1
        if g is not None:
            colors=g.findall('StopColor');positions=g.findall('StopPos')
            if len(colors)!=len(positions):raise ValueError('Mismatched gradient stops')
            self.stops.extend((float(p.text),int(c.text,16)) for p,c in zip(positions,colors))
            fill_type=3 if g.findtext('Radial')=='true' else 2
        if element.findtext('Properties/Fill/Properties/FillType')=='0':fill_type=0
        stop_count=len(self.stops)-stop_start
        image=prop.get('ImagePath') or prop.get('Image')
        if image:values[22]=self.val(22,image)
        node=dict(meta=[parent,kind,branch[0],branch[1],self.string(identity),start,count,stop_start,stop_count,fill_type,
            int(prop.get('ClipChildren')=='true'),int(prop.get('Closed','true')=='true')],
            dimensions=[*design,*path_size],values=values,tag=element.tag,id=identity)
        self.nodes.append(node)
        visual=prop.get('Visual')
        if visual:
            variants={visual}
            # A parent timeline can exchange this control's skin visual.
            for root in self.search_roots:
                for t in root.findall('.//Timelines/Timeline'):
                    if t.findtext('Id')!=identity:continue
                    names=t.findall('TimelineProp')
                    for n,p in enumerate(names):
                        if p.text=='Visual':variants.update(k.findall('Prop')[n].text or '' for k in t.findall('KeyFrame'))
            for name in sorted(variants):
                if not name:continue
                if name in chain:raise ValueError('Recursive skin visual '+name)
                skin=self.visuals.get(name)
                if skin is None:self.missing.add(name);continue
                wrapper=ET.Element('XuiGroup');pp=ET.fromstring(ET.tostring(skin.find('Properties')))
                wrapper.append(pp)
                for key,value in [('Id','@visual:'+name),('Width',str(w)),('Height',str(h))]:
                    entry=pp.find(key)
                    if entry is None:entry=ET.SubElement(pp,key)
                    entry.text=value
                # The visual's children anchor against its authored size.
                sw=float(skin.findtext('Properties/Width') or w);sh=float(skin.findtext('Properties/Height') or h)
                vi=self.add(wrapper,index,(w,h),(index,self.string(name)),(*chain,name))
                for child in skin:
                    if child.tag.startswith('Xui'):self.add(child,vi,(sw,sh),(-1,0),(*chain,name))
                if skin.find('Timelines') is not None:self.pending.append((vi,skin.find('Timelines')))
        for child in element:
            if child.tag.startswith('Xui'):self.add(child,index,(w,h),(-1,0),chain)
        if element.find('Timelines') is not None:self.pending.append((index,element.find('Timelines')))
        return index
    def descendant(self,target,owner):
        for i,node in enumerate(self.nodes):
            if node['id']!=target:continue
            at=i
            while at>=0:
                if at==owner:return i
                at=self.nodes[at]['meta'][0]
        return -1
    def compile(self,main):
        root=ET.parse(main).getroot();scene=root.find('XuiTabScene')
        self.search_roots=[scene,*self.visuals.values()]
        self.add(scene)
        for owner,timelines in self.pending:
            anim_id=len(self.animations);track_start=len(self.tracks);range_start=len(self.ranges);last=0.
            for t in timelines.findall('Timeline'):
                target=self.descendant(t.findtext('Id') or '',owner)
                if target<0:self.unhandled.add('target:'+str(t.findtext('Id')));continue
                names=t.findall('TimelineProp');keys=t.findall('KeyFrame')
                for pi,p in enumerate(names):
                    if p.text not in FIELDS:self.unhandled.add(p.text);continue
                    field=FIELDS.index(p.text);key_start=len(self.keys)
                    for k in keys:
                        original_flag=k.findtext('OriginalFlag')
                        if k.find('OriginalEaseCoefficients') is not None or (original_flag is not None and int(original_flag) not in (0,1,2)):
                            raise ValueError('Unverified original keyframe curve: preserve source metadata before runtime support')
                        entries=k.findall('Prop')
                        if len(entries)!=len(names):raise ValueError('Timeline property count mismatch')
                        time=float(k.findtext('Time'));last=max(last,time)
                        signed=lambda x:float(x)-256 if float(x)>127 else float(x)
                        ease=[signed(k.findtext('EaseIn') or '0'),signed(k.findtext('EaseOut') or '0'),float(k.findtext('EaseScale') or '50')]
                        if ease[2]!=50:raise ValueError('Unimplemented non-default EaseScale')
                        self.keys.append((time,int(k.findtext('Interpolation') or 0),*ease,self.val(field,entries[pi].text or '')))
                    self.tracks.append((anim_id,target,field,int(p.get('index','-1')),key_start,len(self.keys)-key_start))
            for n in timelines.findall('NamedFrames/NamedFrame'):
                commands={'':0,'stop':1,'goto':2,'gotoandplay':3,'gotoandstop':4}
                command=(n.findtext('Command') or '').lower();time=float(n.findtext('Time'));last=max(last,time)
                if command not in commands:raise ValueError('Unsupported named-frame command: '+command)
                target=n.findtext('Parameter') or n.findtext('Target') or ''
                self.ranges.append((self.string(n.findtext('Name')),time,commands.get(command,0),self.string(target)))
            self.animations.append((owner,range_start,len(self.ranges)-range_start,track_start,len(self.tracks)-track_start,last,int(len(self.ranges)==range_start)))
        return self
    def write(self,path):
        path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('wb') as f:
            f.write(b'NXUI0002');f.write(struct.pack('<8I',len(self.nodes),len(self.animations),len(self.tracks),len(self.keys),len(self.ranges),len(self.points),len(self.stops),len(self.strings)))
            for n in self.nodes:
                f.write(struct.pack('<12i4f',*n['meta'],*n['dimensions']))
                for v,u in n['values']:f.write(struct.pack('<4fI',*v,u))
            for a in self.animations:f.write(struct.pack('<5ifi',*a))
            for t in self.tracks:f.write(struct.pack('<6i',*t))
            for frame,mode,ei,eo,es,(v,u) in self.keys:f.write(struct.pack('<fifff4fI',frame,mode,ei,eo,es,*v,u))
            for n in self.ranges:f.write(struct.pack('<IfiI',*n))
            for p in self.points:f.write(struct.pack('<2f',*p))
            for s in self.stops:f.write(struct.pack('<fI',*s))
            f.write(self.strings)
        report=dict(source=self.source,pack_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            nodes=len(self.nodes),animations=len(self.animations),tracks=len(self.tracks),keys=len(self.keys),
            original_curve_bounds_recovered=self.paths_recovered,missing_visuals=sorted(self.missing),
            unhandled_properties=sorted(self.unhandled),fields=FIELDS,
            animated_fields={FIELDS[i]:sum(1 for t in self.tracks if t[2]==i) for i in range(len(FIELDS)) if any(t[2]==i for t in self.tracks)},
            node_index=[dict(index=i,parent=n['meta'][0],id=n['id'],kind=n['tag']) for i,n in enumerate(self.nodes)])
        path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({k:v for k,v in report.items() if k not in ['source','node_index','fields']},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--main',type=Path,required=True);p.add_argument('--main-xur',type=Path,required=True)
    p.add_argument('--skin',type=Path,required=True);p.add_argument('--skin-xur',type=Path,required=True);p.add_argument('--controls',type=Path,required=True);p.add_argument('--controls-xur',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();c=Compiler([(a.skin,a.skin_xur),(a.controls,a.controls_xur),(a.main,a.main_xur)]).compile(a.main);c.write(a.output)
