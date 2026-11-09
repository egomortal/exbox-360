"""Strict decoder for XUIB versions 7 and 8 dashboard scenes.

Reverse-engineered from the files themselves. Emits the same JSON tree shape as
xur5.D so toxml.py can consume it. Every decode asserts that DATA is consumed
exactly, that all supplied header counts match (ten in v7, twelve in v8),
and that every entry of STRN,
FLOT, VECT, QUAT, COLR, CUST, KEYP, KEYD and NAME is referenced; an unknown
class or property bit raises.
"""
import struct,sys,os,copy
TRACE=os.environ.get('TRACE')
SECTION={'f':'FLOT','v':'VECT','q':'QUAT','c4':'COLR'}
def targets(node):
    """Timeline targets: the object's direct children by Id (every v8 track in
    the 12625-17559 builds names a direct child, and no Id repeats)."""
    kids={}
    for c in node.get('children',[]):
        i=c.get('props',{}).get('Id')
        if i is not None:kids[i]=None if i in kids else c
    return kids
class D8:
    def __init__(s,path,levels):
        s.path='<memory>' if isinstance(path,bytes) else path;s.d=path if isinstance(path,bytes) else open(path,'rb').read();d=s.d
        assert d[:4]==b'XUIB' and struct.unpack_from('>I',d,4)[0] in (7,8),'not XUIB v7/v8'
        s.version=struct.unpack_from('>I',d,4)[0]
        assert struct.unpack_from('>I',d,14)[0]==len(d),'file length mismatch'
        s.flags=struct.unpack_from('>I',d,8)[0];s.at=20
        if s.version==8:
            assert s.flags==0,('header flags',s.flags)
            s.counts=[s.packed() for _ in range(12)]
        else:
            assert s.flags in (0,1),('header flags',s.flags)
            s.counts=list(struct.unpack_from('>10I',d,20)) if s.flags else None
            if s.flags:s.at+=40
        s.parts={}
        for _ in range(struct.unpack_from('>H',d,18)[0]):
            name,o,z=struct.unpack_from('>4sII',d,s.at);s.at+=12;assert o+z<=len(d);s.parts[name.decode()]=(o,z)
        s.strings=[''];s.used_str={0}
        if 'STRN' in s.parts:
            # u32 length in characters (not bytes), u16 count, NUL-terminated UTF-8.
            o,z=s.parts['STRN'];chars,n=struct.unpack_from('>IH',d,o);text=d[o+6:o+z].decode('utf-8')
            assert len(text)==chars and text.endswith('\0'),('STRN length',chars,len(text))
            s.strings+=text[:-1].split('\0');assert len(s.strings)==n+1,('STRN count',n,len(s.strings))
        s.levels=levels
        # CUST: records of u32 length, f32 width, f32 height, u32 n, n x 6 f32.
        s.cust=set();s.used_cust=set()
        if 'CUST' in s.parts:
            o,z=s.parts['CUST'];a=0
            while a<z:
                length,w,h,n=struct.unpack_from('>IffI',d,o+a);assert length==12+24*n,('CUST record',a,length,n)
                s.cust.add(a);a+=4+length
            assert a==z,('CUST overrun',a,z)
        s.keyp=s.table_packed('KEYP');s.named=s.read_names();s.keyd=s.read_keys()
        s.ranges={}
        s.inline_keys=s.inline_values=s.inline_names=0
        s.n_obj=s.n_props=s.n_lists=s.n_cprops=s.n_children=s.n_shared=0;s.objects=[];s.plists=[];s.compound={};s.tl=[0,0,0]
        s.used={k:set() for k in SECTION.values()};s.used_keyp=set();s.used_keyd=set();s.used_name=set()
    # --- primitive readers -------------------------------------------------
    def string(s,v):
        assert v<len(s.strings),('string index',v,s.at);s.used_str.add(v);return s.strings[v]
    def byte(s):v=s.d[s.at];s.at+=1;return v
    def packed(s):
        v=s.byte()
        if v<240:return v
        if v<255:return ((v&15)<<8)|s.byte()
        v=struct.unpack_from('>I',s.d,s.at)[0];s.at+=4;return v
    def section(s,name,i,fmt,width):
        o,z=s.parts[name];assert 0<=i and (i+1)*width<=z,(name,i,z,s.at)
        return struct.unpack_from(fmt,s.d,o+i*width)
    def table_packed(s,name):
        if name not in s.parts:return []
        o,z=s.parts[name];save=s.at;s.at=o;out=[]
        while s.at<o+z:out.append(s.packed())
        assert s.at==o+z,(name,'overrun');s.at=save;return out
    def read_names(s):
        if 'NAME' not in s.parts:return []
        o,z=s.parts['NAME'];save=s.at;s.at=o;out=[]
        while s.at<o+z:
            name=s.string(s.packed());t=s.packed();cmd=s.byte();assert cmd<5,('named frame command',cmd)
            out.append([name,t,cmd,s.string(s.packed()) if cmd>1 else ''])
        assert s.at==o+z;s.at=save;return out
    def read_keys(s):
        if 'KEYD' not in s.parts:return []
        o,z=s.parts['KEYD'];save=s.at;s.at=o;out=[]
        while s.at<o+z:
            t=s.packed();flag=s.byte();kind=flag&63
            if kind==2:ext=[s.byte(),s.byte(),s.byte()]
            elif kind==10:ext=[s.packed()]   # VECT index (curve parameters)
            # T15: kinds 5/6/8 have no extension: the next packed value is
            # KEYP. Both Metro skins consume KEYD and all 12 counts exactly.
            # Their curves remain unsupported, retained verbatim in flags.
            elif kind in (0,1,3,4,5,6,8):ext=[]
            else:raise ValueError(('keyframe kind',flag,s.at))
            out.append((t,flag,ext,s.packed()))
        assert s.at==o+z;s.at=save;return out
    # --- values --------------------------------------------------------------
    def scalar(s,t,v):
        if t in SECTION:s.used[SECTION[t]].add(v)
        if t=='raw':return 'RAW@%x'%v
        if t=='s':return s.string(v)
        if t=='f':return round(s.section('FLOT',v,'>f',4)[0],4)
        if t=='v':return tuple(round(x,3) for x in s.section('VECT',v,'>3f',12))
        if t=='q':return tuple(round(x,4) for x in s.section('QUAT',v,'>4f',16))
        if t=='c4':return '%08x'%s.section('COLR',v,'>I',4)[0]
        if t=='i':return v-(1<<32) if v>>31 else v
        if t=='u':return v
        if t=='b':assert v<2,('bool',v);return v
        if t=='c':
            assert v in s.cust,('CUST offset not a record',v);s.used_cust.add(v);return 'CUST@%d'%v
        raise ValueError(t)
    def val(s,t):
        if t.startswith('*'):return [s.scalar(t[1:],s.packed()) for _ in range(s.packed())]
        if t.startswith('o:'):return s.compound_obj(t[2:])
        return s.scalar(t,s.packed())
    def compound_obj(s,kind):
        # Compound (Fill/Stroke/Gradient) values carry an object id. Ids are
        # numbered in post-order; an id seen before is a reference to it.
        if s.version==7:
            v=s.props([OBJ[kind]],compound=True)
            s.compound[len(s.compound)]=(kind,v)
            return v
        i=s.packed()
        if i in s.compound:
            k,v=s.compound[i];assert k==kind,('compound kind',i,k,kind);return copy.deepcopy(v)
        v=s.props([OBJ[kind]],compound=True)
        s.compound[i]=(kind,v);return copy.deepcopy(v)
    def block(s,table,out):
        mask=s.packed();n=0;b=0
        while mask>>b:
            if mask>>b&1:
                if b not in table:raise KeyError(('unknown prop',table.get('__name__'),b,hex(mask),s.at,s.path,s.d[s.at:s.at+12].hex()))
                name,t=table[b];v=s.val(t);out[name]=v;n+=len(v) if t.startswith('*') else 1
            b+=1
        return n
    def props(s,levels,compound=False):
        count=s.packed();out={};n=0
        for t in levels:n+=s.block(t,out)
        assert n==count,('count',count,n,out,s.at,s.path)
        if compound:s.n_cprops+=count
        else:s.n_props+=count
        return out
    # --- tree ------------------------------------------------------------------
    def obj(s,depth=0):
        start=s.at;cls=s.string(s.packed());fl=s.byte();assert fl<16,('object flags',cls,fl,start)
        if cls not in s.levels:raise KeyError(('levels',cls,start,s.path,s.d[start:start+24].hex()))
        if TRACE:print('  '*depth+cls,fl,start-s.parts['DATA'][0],file=sys.stderr)
        node={'class':cls};s.objects.append(node)
        if fl&8:
            # Flag 8 replaces flag 1: the object reuses the property list of an
            # earlier object, by index among the property lists written so far.
            assert not fl&1,('shared and own properties',cls,fl,start)
            ref=s.packed();assert ref<len(s.plists),('shared property list',ref,len(s.plists),start)
            base=s.plists[ref];assert base['class']==cls,('shared property class',cls,base['class'],start)
            node['props']=copy.deepcopy(base['props']);s.n_shared+=1
            if TRACE:print('  '*depth+'  props of list',ref,node['props'],file=sys.stderr)
        if fl&1:
            node['props']=s.props(s.levels[cls]);s.n_lists+=1;s.plists.append(node)
            if TRACE:print('  '*depth+'  ',node['props'],file=sys.stderr)
        if fl&2:
            s.n_children+=1;node['children']=[s.obj(depth+1) for _ in range(s.packed())]
        if fl&4:node['timelines']=s.timelines(node)
        s.ranges[id(node)]=(start,s.at)
        return node
    def timelines(s,node):
        if s.version==7:return s.timelines7(node)
        out={'named':[],'tracks':[]}
        n=s.packed()
        if n:
            first=s.packed();assert first+n<=len(s.named),('NAME range',first,n)
            out['named']=[list(x) for x in s.named[first:first+n]];s.used_name.update(range(first,first+n))
        if not node.get('children'):return out
        kids=targets(node)
        for _ in range(s.packed()):
            target=s.string(s.packed());child=kids.get(target)
            assert child is not None,('timeline target missing or ambiguous',target,s.at)
            levels=list(reversed(s.levels[child['class']]));defs=[];s.tl[0]+=1
            for _ in range(s.packed()):
                depth=s.byte();idx=bool(depth&0x80);depth&=0x7f;ci=s.byte();table=levels[ci];names=[];s.tl[1]+=depth;s.tl[2]+=1
                for k in range(depth):
                    pi=s.byte()
                    if pi not in table:raise KeyError(('timeline prop',target,table.get('__name__'),pi,s.at))
                    name,t=table[pi];names.append(name)
                    if k<depth-1:assert t.startswith('o:'),('path through non-object',names);table=OBJ[t[2:]]
                index=s.packed() if idx else None
                defs.append(('.'.join(names),t.lstrip('*'),index))
            nk=s.packed();keys=[];ext=[]
            if nk:
                first=s.packed();assert first+nk<=len(s.keyd),('KEYD range',first,nk,len(s.keyd))
                s.used_keyd.update(range(first,first+nk))
                for t,flag,e,pi in s.keyd[first:first+nk]:
                    assert pi+len(defs)<=len(s.keyp),('KEYP range',pi,len(defs),len(s.keyp))
                    s.used_keyp.update(range(pi,pi+len(defs)))
                    vals=[s.scalar(tp,s.keyp[pi+j]) for j,(_,tp,_) in enumerate(defs)]
                    kind=flag&63
                    ease=e if kind==2 else [0,0,50]
                    keys.append([t,kind if kind<=2 else 0,ease,vals])
                    ext.append([flag,s.scalar('v',e[0]) if kind==10 else None])
            tr={'target':target,'props':[n for n,_,_ in defs],'index':[i for _,_,i in defs],'keys':keys}
            # Keys whose raw flag is not plain 0/1/2 (bits 0x40/0x80 or extended kinds)
            # keep [raw flag, kind-10 curve vector] here; 'keys' carries interp 0 for them.
            if any(f not in (0,1,2) for f,_ in ext):tr['flags']=ext
            out['tracks'].append(tr)
        return out
    # v7 uses packed inline names/keys, in-place compounds and an optional
    # ten-u32 count header. v8 interns these as NAME/KEYD/KEYP and compound ids.
    # v7 kind 2 adds two FLOT references after its three easing bytes; their
    # curve semantics are not established, so exporters must reject them.
    def keyval7(s,tp):
        s.inline_values+=1
        return s.scalar(tp,s.packed())
    def timelines7(s,node):
        out={'named':[],'tracks':[]}
        for _ in range(s.packed()):
            s.inline_names+=1
            name=s.string(s.packed());tm=s.packed();cmd=s.byte()
            assert cmd<5,('named frame command',cmd)
            out['named'].append([name,tm,cmd,s.string(s.packed()) if cmd>1 else ''])
        kids=targets(node)
        for _ in range(s.packed() if node.get('children') else 0):
            target=s.string(s.packed());child=kids.get(target)
            assert child is not None,('timeline target missing or ambiguous',target,s.at)
            levels=list(reversed(s.levels[child['class']]));defs=[];s.tl[0]+=1
            for _ in range(s.packed()):
                depth=s.byte();idx=bool(depth&0x80);depth&=0x7f;ci=s.byte();table=levels[ci];names=[];s.tl[1]+=depth;s.tl[2]+=1
                for k in range(depth):
                    pi=s.byte();name,tp=table[pi];names.append(name)
                    if k<depth-1:
                        assert tp.startswith('o:'),('path through non-object',names)
                        table=OBJ[tp[2:]]
                defs.append(('.'.join(names),tp.lstrip('*'),s.packed() if idx else None))
            keys=[];extensions=[]
            for _ in range(s.packed()):
                tm=s.packed();flag=s.byte();kind=flag&63
                ease=[s.byte(),s.byte(),s.byte()] if kind==2 else [0,0,50]
                assert kind in (0,1,2,3,4),('v7 key kind',flag,s.at)
                extensions.append([s.scalar('f',s.packed()),s.scalar('f',s.packed())] if kind==2 else None)
                keys.append([tm,flag,ease,[s.keyval7(tp) for _,tp,_ in defs]])
                s.inline_keys+=1
            tr=dict(target=target,props=[n for n,_,_ in defs],index=[i for _,_,i in defs],keys=keys)
            if any(x is not None for x in extensions):tr['v7_curve_parameters']=extensions
            out['tracks'].append(tr)
        return out
    def decode(s):
        o,z=s.parts['DATA'];s.at=o;root=s.obj();s.end=o+z
        assert s.at==s.end,('Unconsumed DATA',s.at,s.end,s.path)
        got=[len(s.objects),s.n_props,s.n_lists,s.n_cprops,len(s.compound),s.tl[1],s.tl[2],s.tl[0],len(s.keyp),len(s.keyd),len(s.named),s.n_children]
        s.got=got
        if s.version==7:
            got=[len(s.objects),s.n_props+s.n_cprops,s.n_lists+len(s.compound),s.inline_values,s.tl[1],s.tl[2],s.inline_keys,s.tl[0],s.inline_names,s.n_children]
            s.got=got
        for i,(a,b) in enumerate(zip(got,s.counts or got)):
            if a is not None:assert a==b,('header count',i,a,b,got,s.counts,s.path)
        # Every value-table entry, KEYP/KEYD entry and named frame is referenced.
        for name,width in [('FLOT',4),('VECT',12),('QUAT',16),('COLR',4)]:
            n=s.parts[name][1]//width if name in s.parts else 0
            assert s.used[name]==set(range(n)),('unreferenced',name,sorted(set(range(n))-s.used[name])[:8],s.path)
        assert s.used_str==set(range(len(s.strings))),('unreferenced strings',[s.strings[i] for i in sorted(set(range(len(s.strings)))-s.used_str)][:8],s.path)
        assert s.used_cust==s.cust,('unreferenced CUST',sorted(s.cust-s.used_cust)[:8],s.path)
        assert s.used_keyp==set(range(len(s.keyp))) and s.used_keyd==set(range(len(s.keyd))) and s.used_name==set(range(len(s.named))),('unreferenced timeline data',s.path)
        return root
from classes8 import *
def prop_types(cls):
    out={}
    for level in L[cls]:
        for b,v in level.items():
            if b!='__name__':out.setdefault(v[0],v[1])
    return out
def for_xml(root):
    """A copy of a decoded tree ready for toxml.node(): title classes become
    their XUI base class, bools become True/False and colours stored under
    placeholder names get the 0x prefix the compiler expects."""
    def value(name,t,v):
        t=t.lstrip('*')
        if t=='b':return bool(v)
        if t=='c4' and not name.endswith('Color'):return '0x'+v
        return v
    def props(items,types):
        out={}
        for k,v in items.items():
            t=types[k]
            if t.startswith('o:'):out[k]=props(v,{e[0]:e[1] for b,e in OBJ[t[2:]].items() if b!='__name__'})
            elif t.startswith('*'):out[k]=[value(k,t,x) for x in v]
            else:out[k]=value(k,t,v)
        return out
    def path_type(types,path):
        parts=path.split('.');t=types[parts[0]]
        for p in parts[1:]:t=next(e[1] for b,e in OBJ[t[2:]].items() if b!='__name__' and e[0]==p)
        return t
    def walk(n):
        cls=n['class'];types=prop_types(cls);base=cls
        while base in CUSTOM:base=CUSTOM[base]
        out={'class':base}
        if 'props' in n:out['props']=props(n['props'],types)
        if 'children' in n:out['children']=[walk(c) for c in n['children']]
        if 'timelines' in n:
            tl=n['timelines'];out['timelines']={'named':[list(x) for x in tl['named']],'tracks':[]}
            kids=targets(n)
            for tr in tl['tracks']:
                tt=prop_types(kids[tr['target']]['class'])
                ts=[path_type(tt,p) for p in tr['props']]
                keys=[[k[0],k[1],list(k[2]),[value(p.split('.')[-1],t,v) for p,t,v in zip(tr['props'],ts,k[3])]] for k in tr['keys']]
                out['timelines']['tracks'].append(dict(tr,keys=keys))
        return out
    return walk(root)
def decode(path):
    d=D8(path,L);return d.decode(),d
