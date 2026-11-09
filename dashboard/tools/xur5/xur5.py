"""Decoder for the launch-era XUIB version 5 scene format (1888 Guide).
Reverse-engineered from the files themselves; asserts full DATA consumption."""
import struct,sys,json
E={0:('Id','s'),1:('Width','f'),2:('Height','f'),3:('Position','v'),4:('Scale','v'),5:('Rotation','q'),6:('Opacity','f'),
   7:('Anchor','i'),8:('Pivot','v'),9:('Show','b'),10:('BlendMode','i'),11:('DisableTimelineRecursion','b'),12:('DesignTime','b'),13:('ColorWriteFlags','i'),14:('ClipChildren','b')}
CONTROL={0:('ClassOverride','s'),1:('Visual','s'),2:('Enabled','b'),3:('UnfocussedInput','b'),4:('NavLeft','s'),5:('NavRight','s'),6:('NavUp','s'),7:('NavDown','s'),8:('Text','s'),9:('Ctrl9','s'),10:('ImagePath','s'),11:('Ctrl11','s'),12:('Ctrl12','s')}
SCENE={0:('DefaultFocus','s'),1:('TransFrom','s'),2:('TransTo','s'),3:('TransBackFrom','s'),4:('TransBackTo','s'),5:('InterruptTransitions','b'),6:('IgnorePresses','b')}
BUTTON={0:('PressKey','i')}
FIG={0:('Stroke','o:Stroke'),1:('Fill','o:Fill'),2:('Closed','b'),3:('Points','c')}
STROKE={0:('StrokeWidth','f'),1:('StrokeColor','c4')}
FILL={0:('FillType','i'),1:('FillColor','c4'),2:('TextureFileName','s'),3:('Gradient','o:Gradient'),4:('Translation','v'),5:('Scale','v'),6:('Rotation','f'),7:('WrapX','i'),8:('WrapY','i'),9:('BrushFlags','i')}
GRAD={0:('Radial','b'),1:('NumStops','i'),2:('StopColor','*c4'),3:('StopPos','*f')}
IMAGE={0:('SizeMode','i'),1:('ImagePath','s'),2:('Brightness','f'),3:('Contrast','f')}
TEXT={0:('TextColor','c4'),1:('DropShadowColor','c4'),2:('PointSize','f'),3:('Font','s'),4:('TextStyle','i'),5:('LineSpacingAdjust','si'),6:('Text6','i')}
LIST={}
CLASSES={}
import os
TRACE=os.environ.get('TRACE')
def lv(*levels):return [E,{}]+list(levels)
class D:
    def __init__(s,path,levels,types=None):
        s.d=path if isinstance(path,bytes) else open(path,'rb').read();assert s.d[:4]==b'XUIB' and struct.unpack_from('>I',s.d,4)[0]==5
        assert struct.unpack_from('>I',s.d,14)[0]==len(s.d),'file length mismatch'
        flags=struct.unpack_from('>I',s.d,8)[0];n=struct.unpack_from('>H',s.d,18)[0];at=20+(40 if flags&1 else 0)
        s.parts={}
        for i in range(n):
            name,o,z=struct.unpack_from('>4sII',s.d,at+12*i);s.parts[name.decode()]=(o,z)
        o,z=s.parts['STRN'];s.strings=['']
        a=o
        while a<o+z:
            l=struct.unpack_from('>H',s.d,a)[0];a+=2;s.strings.append(s.d[a:a+2*l].decode('utf-16be'));a+=2*l
        s.levels=levels;s.at=0;s.unknown=set();s.ranges={}
    def string(s,i):
        assert 0<=i<len(s.strings),('string index',i,s.at)
        return s.strings[i]
    def u8(s):v=s.d[s.at];s.at+=1;return v
    def u16(s):v=struct.unpack_from('>H',s.d,s.at)[0];s.at+=2;return v
    def u32(s):v=struct.unpack_from('>I',s.d,s.at)[0];s.at+=4;return v
    def f(s):v=struct.unpack_from('>f',s.d,s.at)[0];s.at+=4;return round(v,4)
    def vect(s,i):
        o,z=s.parts['VECT'];assert 0<=i and (i+1)*12<=z,('VECT index',i,z);return tuple(round(x,3) for x in struct.unpack_from('>3f',s.d,o+12*i))
    def quat(s,i):
        o,z=s.parts['QUAT'];assert 0<=i and (i+1)*16<=z,('QUAT index',i,z);return tuple(round(x,4) for x in struct.unpack_from('>4f',s.d,o+16*i))
    def val(s,t):
        if t=='raw8':return 'RAW8@%02x'%s.u8()
        if t=='raw32':return 'RAW32@%08x'%s.u32()
        if t=='s':return s.string(s.u16())
        if t=='f':return s.f()
        if t=='v':return s.vect(s.u32())
        if t=='q':return s.quat(s.u32())
        if t=='i':return s.u32()
        if t=='si':v=s.u32();return v-(1<<32) if v>>31 else v
        if t=='b':v=s.u8();assert v<2,('bool',v);return v
        if t=='0':return True
        if t=='c4':return '%08x'%s.u32()
        if t=='c':return 'CUST@%d'%s.u32()
        if t.startswith('*'):
            n=s.u8();return [s.val(t[1:]) for _ in range(n)]
        if t.startswith('o:'):return s.props(OBJ[t[2:]])
        raise ValueError(t)
    def block(s,table,out):
        x=s.u8();nb=x&7;mask=int.from_bytes(s.d[s.at:s.at+nb],'big');s.at+=nb
        if nb==0:return 0
        assert (x>>3)+1==bin(mask).count('1'),('popcount',hex(x),hex(mask),s.at)
        n=0
        for b in range(nb*8):
            if mask>>b&1:
                if b not in table:raise KeyError(('unknown prop',b,table.get('__name__'),s.at,s.d[s.at:s.at+12].hex()))
                name,t=table[b];v=s.val(t);out[name]=v;n+=len(v) if t.startswith('*') else 1
        return n
    def props(s,levels):
        count=s.u16();out={};n=0
        for t in levels:n+=s.block(t,out)
        assert n==count,('count',count,n,out,s.at)
        return out
    def plausible(s,fl):
        # After the properties: a child count, or the next record (2 = a clean record start).
        at=s.at;end=s.parts['DATA'][0]+s.parts['DATA'][1]
        if at>=end:return 2 if at==end else 0
        if fl&2:return 2 if struct.unpack_from('>I',s.d,at)[0]<4096 else 0
        if at+5<=len(s.d):
            c=struct.unpack_from('>H',s.d,at)[0];f=s.d[at+2];n=struct.unpack_from('>H',s.d,at+3)[0]
            if 0<c<len(s.strings) and f<8 and s.strings[c][:1].isalpha() and (not f&1 or 0<n<64):return 2
        return 1 if struct.unpack_from('>I',s.d,at)[0]<4096 else 0
    def obj(s,depth=0):
        start=s.at;cls=s.string(s.u16());fl=s.u8();assert fl<8,(cls,fl,start)
        node={'class':cls}
        if TRACE:print('  '*depth+cls,fl,start-s.parts['DATA'][0],file=sys.stderr)
        if fl&1:
            if cls not in s.levels:raise KeyError(('levels',cls,start,s.d[start:start+40].hex()))
            options=s.levels[cls]
            if options and isinstance(options[0],list):
                # A custom class whose own level is not known: take the first
                # candidate whose property count matches and leaves a plausible next record.
                save=s.at;last=None;best=None
                for levels in options:
                    s.at=save
                    try:
                        props=s.props(levels);score=s.plausible(fl)
                        if score and (best is None or score>best[0]):best=(score,s.at,props)
                    except (AssertionError,KeyError,IndexError,struct.error,UnicodeDecodeError) as e:last=e
                if best is None:raise KeyError(('no candidate fits',cls,start,last))
                s.at=best[1];node['props']=best[2]
            else:node['props']=s.props(options)
            if TRACE:print('  '*depth+'  ',node['props'],hex(s.at-s.parts['DATA'][0]),file=sys.stderr)
        if fl&2:node['children']=[s.obj(depth+1) for _ in range(s.u32())]
        if fl&4:node['timelines']=s.timelines(node)
        s.ranges[id(node)]=(start,s.at)
        return node
    def timelines(s,node):
        out={'named':[],'tracks':[]}
        for _ in range(s.u32()):
            name=s.string(s.u16());t=s.u32();cmd=s.u8();par=s.string(s.u16())
            out['named'].append([name,t,cmd,par])
        kids={c.get('props',{}).get('Id'):c for c in node.get('children',[])}
        for _ in range(s.u32() if node.get('children') else 0):
            tat=s.at;target=s.string(s.u16());child=kids.get(target)
            if TRACE:print('track',target,hex(tat-s.parts['DATA'][0]),file=sys.stderr);assert child is not None,('timeline target',target)
            assert child is not None,('timeline target missing',target,s.at)
            levels=list(reversed(s.levels[child['class']]));defs=[]
            for _ in range(s.u32()):
                depth=s.u8();idx=bool(depth&0x80);depth&=0x7f;ci=s.u8();table=levels[ci];names=[]
                for k in range(depth):
                    pi=s.u8();name,t=table[pi];names.append(name)
                    if t.startswith('o:'):table=OBJ[t[2:]][0]
                index=s.u32() if idx else None
                defs.append(('.'.join(names),t.lstrip('*'),index))
            keys=[]
            for _ in range(s.u32()):
                tm=s.u32();interp=s.u8();ease=[s.u8(),s.u8(),s.u8()]
                keys.append([tm,interp,ease,[s.val(t) for _,t,_ in defs]])
            out['tracks'].append({'target':target,'props':[n for n,_,_ in defs],'index':[i for _,_,i in defs],'keys':keys})
        return out
    def decode(s):
        o,z=s.parts['DATA'];s.at=o;root=s.obj();s.end=o+z
        assert s.at==s.end,('Unconsumed DATA',s.at,s.end)
        return root
OBJ={}
for name,t in [('E',E),('Control',CONTROL),('Scene',SCENE),('Button',BUTTON),('Figure',FIG),('Stroke',STROKE),('Fill',FILL),('Gradient',GRAD),('Image',IMAGE),('Text',TEXT)]:
    t['__name__']=name
OBJ={'Stroke':[STROKE],'Fill':[FILL],'Gradient':[GRAD]}
