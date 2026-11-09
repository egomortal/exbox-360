"""Preserve embedded original shader slices and decode the bounded XUI gradient path.

Instruction fields/semantics reference pinned Xenia sources retained alongside
the evidence. Unknown instructions fail closed; no Xbox code or GPU is executed.
"""
import argparse, collections, hashlib, json, math, struct
from pathlib import Path
import numpy as np

VECTOR={0:'add',1:'mul',2:'max',5:'sgt',7:'sne',11:'mad',12:'cndeq',15:'dp4',17:'dp2add'}
SCALAR={0:'adds',40:'sqrt'}


def sha(data):return hashlib.sha256(data).hexdigest()


def alu(words):
    a,b,c=words
    sources=[]
    for number in range(1,4):
        shift=(3-number)*8;register=(c>>shift)&255;swizzle=(b>>shift)&255
        sources.append(dict(register=register,temporary=bool(c&(1<<(32-number))),
            swizzle=[((swizzle>>(2*i))+i)&3 for i in range(4)],negate=bool(b&(1<<(27-number)))))
    result=dict(kind='alu',raw=[f'{v:08x}' for v in words],vector_opcode=(c>>24)&31,
        vector_destination=a&63,vector_mask=(a>>16)&15,scalar_opcode=a>>26,
        scalar_destination=(a>>8)&63,scalar_mask=(a>>20)&15,export=bool(a&(1<<15)),
        vector_clamp=bool(a&(1<<24)),scalar_clamp=bool(a&(1<<25)),sources=sources)
    if result['vector_mask'] and result['vector_opcode'] not in VECTOR:
        raise ValueError('Unsupported active vector opcode '+str(result['vector_opcode']))
    if result['scalar_mask'] and result['scalar_opcode'] not in SCALAR:
        raise ValueError('Unsupported active scalar opcode '+str(result['scalar_opcode']))
    if a&((1<<6)|(1<<7)|(1<<14)) or b&0xf8000000:
        raise ValueError('Unsupported relative/predicated/absolute-constant ALU mode')
    result['vector_name']=VECTOR.get(result['vector_opcode'])
    result['scalar_name']=SCALAR.get(result['scalar_opcode'])
    return result


def fetch(words):
    a,b,c=words
    if a&31!=1:raise ValueError('Unsupported fetch opcode '+str(a&31))
    if a&((1<<11)|(1<<18)) or b&(1<<31) or c&1:
        raise ValueError('Unsupported relative/predicated/gradient texture fetch')
    return dict(kind='fetch',raw=[f'{v:08x}' for v in words],source=(a>>5)&63,destination=(a>>12)&63,
        fetch_constant=(a>>20)&31,unnormalized=bool(a&(1<<25)),
        source_swizzle=[(a>>(26+2*i))&3 for i in range(3)],
        destination_swizzle=[(b>>(3*i))&7 for i in range(4)],
        mag_filter=(b>>12)&3,min_filter=(b>>14)&3,mip_filter=(b>>16)&3,
        dimension=(c>>14)&3,offsets_half_texel=[((c>>shift)&31)-32 if c&(1<<(shift+4)) else (c>>shift)&31 for shift in (16,21,26)])


def vertex_fetch(words):
    a,b,c=words
    if a&31:raise ValueError('Unsupported vertex fetch opcode')
    if a&((1<<11)|(1<<18)) or b&((1<<30)|(1<<31)) or c&(1<<31):
        raise ValueError('Relative, predicated or mini vertex fetch unsupported')
    return dict(kind='vertex_fetch',raw=[f'{v:08x}' for v in words],source=(a>>5)&63,destination=(a>>12)&63,
        fetch_constant=((a>>20)&31)*3+((a>>25)&3),source_swizzle=a>>30,
        destination_swizzle=[(b>>(3*i))&7 for i in range(4)],format=(b>>16)&63,stride=c&255,
        offset=((c>>8)&0x7fffff)-(0x800000 if c&(1<<30) else 0),
        limits='Raw declaration placeholder fields retained; supplied fetch inputs do not emulate stream/declaration patching.')


def decode(raw,vertex=False):
    if not raw or len(raw)%12:raise ValueError('Microcode must contain complete 96-bit units')
    words=struct.unpack('>'+str(len(raw)//4)+'I',raw)
    blocks=[];control=[];cf_limit=len(words)//3;end=False
    for unit in range(cf_limit):
        a,b,c=words[unit*3:unit*3+3]
        for low,high in [(a,b&65535),((b>>16)|(c<<16)&0xffffffff,c>>16)]:
            opcode=(high>>12)&15
            if opcode==0:continue
            if opcode==12:
                control.append(dict(opcode='alloc',type=(high>>9)&3,size=low&7));continue
            if opcode not in (1,2):raise ValueError('Unsupported control-flow opcode '+str(opcode))
            address=low&4095;count=(low>>12)&7;sequence=(low>>16)&4095
            cf_limit=min(cf_limit,address)
            if address+count>len(words)//3:raise ValueError('Execution block exceeds microcode')
            control.append(dict(opcode='exec_end' if opcode==2 else 'exec',address=address,count=count,sequence=sequence))
            for index in range(count):
                ins=words[(address+index)*3:(address+index+1)*3]
                blocks.append(((vertex_fetch if vertex else fetch) if sequence&(1<<(index*2)) else alu)(ins))
            if opcode==2:end=True
        if end:break
        if unit+1>=cf_limit:break
    if not end:raise ValueError('No supported end instruction')
    return dict(control_flow=control,instructions=blocks)


def execute(program,uv):
    registers={0:np.array([uv[0],uv[1],0,0],dtype=np.float64)}
    constants={0:np.ones(4),1:np.ones(4),255:np.zeros(4)}
    exported=None;coordinates=[]
    for instruction in program['instructions']:
        if instruction['kind']=='fetch':
            if instruction['unnormalized'] or instruction['offsets_half_texel']!=[0,0,0]:
                raise ValueError('Probe only supports normalized, unoffset fetch')
            source=registers[instruction['source']]
            coordinates.append([float(source[i]) for i in instruction['source_swizzle']])
            # Color result is a sentinel for checking the final ColorFactor MUL.
            registers[instruction['destination']]=np.array([.2,.4,.6,.8])
            continue
        values=[np.zeros(4) for _ in range(3)]
        needed=set(range(3 if instruction['vector_opcode'] in (11,12,17) else 2)) if instruction['vector_mask'] else set()
        if instruction['scalar_mask']:needed.add(2)
        for source_index in needed:
            source=instruction['sources'][source_index]
            register=source['register']
            if source['temporary']:
                value=registers.get(register&63,np.zeros(4)).copy()
                if register&64:raise ValueError('Relative source not supported')
                if register&128:value=np.abs(value)
            else:
                if register not in constants:raise ValueError('Unbound constant '+str(register))
                value=constants[register].copy()
            value=value[source['swizzle']]
            values[source_index]=-value if source['negate'] else value
        vector=scalar=None
        if instruction['vector_mask']:
            op=instruction['vector_opcode']
            if op==0:vector=values[0]+values[1]
            elif op==1:vector=values[0]*values[1]
            elif op==2:vector=np.maximum(values[0],values[1])
            elif op==5:vector=(values[0]>values[1]).astype(np.float64)
            elif op==7:vector=(values[0]!=values[1]).astype(np.float64)
            elif op==11:vector=values[0]*values[1]+values[2]
            elif op==12:vector=np.where(values[0]==0,values[1],values[2])
            elif op==15:vector=np.full(4,float(np.dot(values[0],values[1])))
            elif op==17:vector=np.full(4,float(np.dot(values[0][:2],values[1][:2])+values[2][0]))
            if instruction['vector_clamp']:vector=np.clip(vector,0,1)
        if instruction['scalar_mask']:
            op=instruction['scalar_opcode']
            if op==0:scalar=values[2][0]+values[2][1]
            elif op==40:scalar=math.sqrt(values[2][0])
            if instruction['scalar_clamp']:scalar=min(1,max(0,scalar))
        if instruction['export']:
            if instruction['scalar_mask']:raise ValueError('Scalar export unsupported')
            exported=vector
        else:
            for mask,dest,value in [(instruction['vector_mask'],instruction['vector_destination'],vector),
                                    (instruction['scalar_mask'],instruction['scalar_destination'],scalar)]:
                if not mask:continue
                target=registers.setdefault(dest,np.zeros(4))
                for component in range(4):
                    if mask&(1<<component):target[component]=value[component] if isinstance(value,np.ndarray) else value
    return coordinates,exported


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    blobs=a.output/'original-shaders';blobs.mkdir(exist_ok=True)
    images=[('xam',0x81870000,1888),('dash6717',0x92000000,6717),('dash',0x92000000,None)]
    if (a.runtime/'dash6717-recovered.pe').exists():images.append(('dash6717-recovered',0x92000000,6717))
    records=[];known={}
    image_records=[]
    for name,base,revision in images:
        path=a.runtime/(name+'.pe');raw=path.read_bytes();image_sha=sha(raw)
        image_records.append(dict(path=str(path),sha256=image_sha,revision=revision))
        for offset in range(0,len(raw)-24,4):
            if raw[offset:offset+4] not in (b'\x10\x2a\x0e\x00',b'\x10\x2a\x0e\x01'):continue
            magic,code_offset,code_size=struct.unpack_from('>3I',raw,offset)
            if not (24<=code_offset<=65536 and 0<code_size<=65536 and code_size%12==0 and offset+code_offset+code_size<=len(raw)):continue
            container=raw[offset:offset+code_offset+code_size];code=container[code_offset:]
            identity=sha(container);code_identity=sha(code)
            (blobs/(identity+'.xshader')).write_bytes(container)
            (blobs/(code_identity+'.ucode')).write_bytes(code)
            entry=dict(image=name,image_sha256=image_sha,revision=revision,source_offset=offset,source_address=hex(base+offset),
                shader_type='vertex' if magic&1 else 'pixel',container_sha256=identity,code_sha256=code_identity,
                container_bytes=len(container),code_offset=code_offset,code_bytes=code_size,
                provenance='original_byte_slice',original_source_bytes_retained=True,
                binding='unknown; preserve for runtime binding audit')
            if name=='xam' and base+offset in (0x81875930,0x81875a30,0x81875b60):
                role={0x81875930:'solid',0x81875a30:'linear',0x81875b60:'radial'}[base+offset]
                entry['binding']='1888 XUI device pixel-shader table slot '+str(['solid','linear','radial'].index(role))
                entry['role']=role
                try:entry['decoded']=decode(code)
                except ValueError as exc:entry['decode_error']=str(exc)
                known[role]=entry
            records.append(entry)
    assert set(known)=={'solid','linear','radial'}
    assert not any('decode_error' in entry for entry in known.values()),known
    rejected=0
    for malformed in (b'',b'\0',struct.pack('>3I',0,0xf000,0)):
        try:decode(malformed)
        except ValueError:rejected+=1
        else:raise AssertionError('Unsupported/truncated program was accepted')
    for name in ('linear','radial'):
        assert known[name]['decoded']['instructions'][-1]['export']
    checks=[]
    for role in ('linear','radial'):
        program=known[role]['decoded'];maximum=0;count=0
        for x in np.linspace(-1,1,41):
            for y in np.linspace(-1,1,41):
                coordinates,color=execute(program,(x,y));assert len(coordinates)==1
                expected=float(x) if role=='linear' else 2*math.hypot(x,y)
                maximum=max(maximum,abs(coordinates[0][0]-expected))
                assert np.max(np.abs(color-[.2,.4,.6,.8]))<1e-12
                count+=1
        assert maximum<1e-12
        checks.append(dict(role=role,samples=count,max_coordinate_error=maximum,
            formula='u' if role=='linear' else '2 * sqrt(u*u + v*v)',
            constants='ColorFactor c1=1 and c255=0; constant-table and zero literal block retained in original containers'))
    for entry in records:
        matches=[role for role,known_entry in known.items() if known_entry['code_sha256']==entry['code_sha256']]
        if matches:entry['code_identity_candidates']=matches
    report=dict(images=image_records,shaders=records,coordinate_checks=checks,unsupported_inputs_rejected=rejected,
        unique_containers=len({r['container_sha256'] for r in records}),unique_microcode=len({r['code_sha256'] for r in records}),
        limits=['Bounded arithmetic/field decoder only; no GPU, host services, interpolation precision or texture sampling emulation.',
            'Same microcode bytes across images do not independently prove runtime binding.',
            'Non-Blades image revision is unknown until provenance is established; later-era shader coverage remains incomplete.',
            'Exact sampler state translation, filtering precision and gamma remain unverified.'])
    (a.output/'shader-recovery.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(containers=report['unique_containers'],microcode=report['unique_microcode'],occurrences=len(records),checks=checks)))


if __name__=='__main__':main()
