"""Convert a decoded XUIB v5 scene into the XUI XML the compiler consumes."""
import sys,struct,xml.etree.ElementTree as ET
sys.path.insert(0,str(__import__('pathlib').Path(__file__).resolve().parent))
from xur5 import *
from classes5 import *
BOOLS={'Show','Enabled','UnfocussedInput','Closed','Radial','ClipChildren','DisableTimelineRecursion','DesignTime','GC1','LI1','ReadOnly','Multiline','InterruptTransitions','IgnorePresses','MeshOnly'}
def fmt(name,v,d):
    if isinstance(v,bool):return 'true' if v else 'false'
    if name in BOOLS:return 'true' if v else 'false'
    if isinstance(v,(list,tuple)):return ','.join('%.9g'%x for x in v)
    if isinstance(v,float):return '%.9g'%v
    if isinstance(v,int):return str(v)
    if isinstance(v,str) and v.startswith('CUST@'):
        off=d.parts['CUST'][0]+int(v[5:]);length,w,h,n=struct.unpack_from('>IffI',d.d,off)
        pts=[struct.unpack_from('>6f',d.d,off+16+24*i) for i in range(n)]
        return str(n)+','+','.join(','.join('%.9g'%x for x in p)+',0' for p in pts)+','
    if isinstance(v,str) and len(v)==8 and name.endswith('Color'):return '0x'+v
    return v
def props(parent,items,d):
    box=ET.SubElement(parent,'Properties')
    for k,v in items.items():
        if isinstance(v,dict):props(ET.SubElement(box,k),v,d);continue
        if k in ('StopColor','StopPos'):
            for x in v:ET.SubElement(box,k).text=('0x'+x) if k=='StopColor' else '%.9g'%x
            continue
        ET.SubElement(box,k).text=fmt(k,v,d)
DEFAULT_VISUAL={'XuiButton':'XuiButton','XuiNavButton':'XuiButton','XuiBackButton':'XuiBackButton','XuiLabel':'XuiLabel','XuiList':'XuiList',
 'XuiCheckbox':'XuiCheckbox','XuiSlider':'XuiSlider','XuiEdit':'XuiEdit','XuiRadioButton':'XuiRadioButton','XuiProgressBar':'XuiProgressBar','XuiCaret':'XuiCaret'}
def xui_tag(cls):
    # A title's own class (GuideDashCommandNavButton, HUDScene...) draws as its XUI base.
    if cls.startswith('Xui'):return cls
    for suffix,base in [('NavButton','XuiNavButton'),('BackButton','XuiBackButton'),('Button','XuiButton'),('TabScene','XuiTabScene'),
                        ('List','XuiList'),('Label','XuiLabel'),('Image','XuiImage'),('Control','XuiControl')]:
        if cls.endswith(suffix):return base
    return 'XuiScene'
def node(n,d):
    for track in n.get('timelines',{}).get('tracks',[]):
        if (any(flag not in (0,1,2) for flag,_ in track.get('flags',[])) or
                any(k[1] not in (0,1,2) for k in track['keys'])):
            raise ValueError('Unsupported key flags: '+track['target'])
        if track.get('v7_curve_parameters'):
            raise ValueError('Unsupported v7 curve parameters: '+track['target'])
    e=ET.Element(xui_tag(n['class']))
    if n['class'] in DEFAULT_VISUAL and 'Visual' not in n.get('props',{}):
        n.setdefault('props',{})['Visual']=DEFAULT_VISUAL[n['class']]
    props(e,n.get('props',{}),d)
    for c in n.get('children',[]):e.append(node(c,d))
    t=n.get('timelines')
    if t:
        tl=ET.SubElement(e,'Timelines');nf=ET.SubElement(tl,'NamedFrames')
        for name,time,cmd,par in t['named']:
            f=ET.SubElement(nf,'NamedFrame');ET.SubElement(f,'Name').text=name;ET.SubElement(f,'Time').text=str(time)
            if cmd:ET.SubElement(f,'Command').text=['','stop','goto','gotoandplay','gotoandstop'][cmd]
            if par:ET.SubElement(f,'Parameter').text=par
        for tr in t['tracks']:
            x=ET.SubElement(tl,'Timeline');ET.SubElement(x,'Id').text=tr['target']
            for p,ix in zip(tr['props'],tr.get('index',[None]*len(tr['props']))):
                tp=ET.SubElement(x,'TimelineProp');tp.text={'ImagePath':'Fill.TextureFileName','Path':'Fill.TextureFileName'}.get(p,p)
                if ix is not None:tp.set('index',str(ix))
            for time,interp,ease,vals in tr['keys']:
                k=ET.SubElement(x,'KeyFrame');ET.SubElement(k,'Time').text=str(time);ET.SubElement(k,'Interpolation').text=str(interp)
                for nm,v in zip(['EaseIn','EaseOut','EaseScale'],ease):ET.SubElement(k,nm).text=str(v)
                for p,v in zip(tr['props'],vals):ET.SubElement(k,'Prop').text=fmt(p.split('.')[-1],v,d)
    return e
def convert8(src):
    # XUIB version 8 (2.0.12625+ dash): strict decoder in xur8.py / classes8.py.
    import xur8
    r,d=xur8.decode(src)
    return node(xur8.for_xml(r),d),d
def convert(src):
    if open(src,'rb').read(8) in (b'XUIB\0\0\0\x07',b'XUIB\0\0\0\x08'):return convert8(src)
    d=D(src,L);r=d.decode();assert d.at==d.end
    return node(r,d),d
if __name__=='__main__':
    root,d=convert(sys.argv[1]);ET.indent(root);ET.ElementTree(root).write(sys.argv[2],encoding='utf-8')
