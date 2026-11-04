"""Verify bounded original vertex arithmetic with supplied stream inputs/constants."""
from pathlib import Path
import argparse, hashlib, json, struct
import numpy as np
import recover_xui_gradient_shaders as microcode

COMPONENTS='xyzw'

def constant_table(container):
    base=struct.unpack_from('>I',container,16)[0]+4
    size,creator,version,count,info,flags,target=struct.unpack_from('>7I',container,base)
    if size!=28 or count>256:raise ValueError('Unsupported constant table')
    def text(offset):
        start=base+offset;end=container.index(0,start)
        return container[start:end].decode('ascii')
    entries=[]
    for index in range(count):
        name,register_set,register,count,reserved,type_offset,defaults=struct.unpack_from('>I4H2I',container,base+info+index*20)
        entries.append(dict(name=text(name),register_set=register_set,register=register,count=count))
    return entries

def execute(program,inputs,constants):
    registers={};exports={};trace=[];fetches=0
    for instruction in program['instructions']:
        if instruction['kind']=='vertex_fetch':
            if fetches>=len(inputs):raise ValueError('Unbound supplied vertex input')
            source=np.asarray(inputs[fetches],dtype=np.float64);fetches+=1
            target=registers.setdefault(instruction['destination'],np.full(4,np.nan))
            for component,select in enumerate(instruction['destination_swizzle']):
                if select<4:target[component]=source[select]
                elif select in (4,5):target[component]=select-4
                elif select!=7:raise ValueError('Unsupported vertex fetch swizzle')
            trace.append(dict(operation='supplied_vertex_input',destination=instruction['destination'],value=[None if not np.isfinite(v) else float(v) for v in target]))
            continue
        if instruction['kind']!='alu' or instruction['scalar_mask']:raise ValueError('Unsupported vertex instruction')
        op=instruction['vector_opcode'];needed=3 if op in (11,12,17) else 2
        values=[];labels=[]
        for source in instruction['sources'][:needed]:
            register=source['register']
            if source['temporary']:
                if register&64:raise ValueError('Relative temporary not supported')
                value=registers[register&63].copy();label='r'+str(register&63)
                if register&128:value=np.abs(value);label='abs('+label+')'
            else:value=np.asarray(constants[register],dtype=np.float64).copy();label='c'+str(register)
            value=value[source['swizzle']]
            values.append(-value if source['negate'] else value)
            labels.append(('-' if source['negate'] else '')+label+'.'+''.join(COMPONENTS[c] for c in source['swizzle']))
        if op==0:value=values[0]+values[1]
        elif op==1:value=values[0]*values[1]
        elif op==2:value=np.maximum(values[0],values[1])
        elif op==5:value=(values[0]>values[1]).astype(np.float64)
        elif op==7:value=(values[0]!=values[1]).astype(np.float64)
        elif op==11:value=values[0]*values[1]+values[2]
        elif op==12:value=np.where(values[0]==0,values[1],values[2])
        elif op==15:value=np.full(4,np.dot(values[0],values[1]))
        elif op==17:value=np.full(4,np.dot(values[0][:2],values[1][:2])+values[2][0])
        else:raise ValueError('Unsupported vector operation')
        if instruction['vector_clamp']:value=np.clip(value,0,1)
        dest=instruction['vector_destination'];mask=instruction['vector_mask']
        if instruction['scalar_mask']:raise ValueError('Special export masks unsupported')
        target=(exports if instruction['export'] else registers).setdefault(dest,np.full(4,np.nan))
        for component in range(4):
            if mask&(1<<component):
                if not np.isfinite(value[component]):raise ValueError('Uninitialized or nonfinite output')
                target[component]=value[component]
        trace.append(dict(operation=instruction['vector_name'],destination=('o' if instruction['export'] else 'r')+str(dest)+'.'+''.join(COMPONENTS[c] for c in range(4) if mask&(1<<c)),sources=labels,value=[None if not np.isfinite(v) else float(v) for v in target]))
    return exports,trace

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    recovery=json.loads((a.assets/'blades-gradient-texture-20261026/shader-recovery.json').read_text())
    shaders={r['source_address']:r for r in recovery['shaders'] if r['image']=='xam'}
    records=[]
    for address in ('0x81875470','0x818756e0'):
        source=shaders[address];container=(a.assets/'blades-gradient-texture-20261026/original-shaders'/(source['container_sha256']+'.xshader')).read_bytes()
        assert hashlib.sha256(container).hexdigest()==source['container_sha256']
        code=container[source['code_offset']:];program=microcode.decode(code,vertex=True)
        constants={i:np.array([0.,0.,0.,0.]) for i in range(12)}
        for index,row in enumerate(np.eye(4)):constants[index]=row.copy();constants[index+4]=row.copy()
        constants[8]=np.array([0,.25,2,4]);constants[9]=np.zeros(4)
        constants[10]=np.array([1,0,0,0]);constants[11]=np.array([0,1,0,0])
        literal=np.asarray(struct.unpack_from('>4f',container,0x34));constants[255]=literal
        outputs,trace=execute(program,[[13,27,0,0],[.2,.8,0,0]],constants)
        rng=np.random.default_rng(1888);maximum_clip=0;maximum_uv=0;maximum_phase=0
        for sample in range(512):
            streams=[rng.uniform(-100,100,4),rng.uniform(-100,100,4)]
            c={i:rng.uniform(-2,2,4) for i in range(12)};c[255]=literal
            matrix=rng.uniform(-2,2,(4,4));matrix[3]=[0,0,0,1]
            if sample%4==0:matrix[0,:2]=0
            radial=bool(sample%2)
            c[10]=matrix[0].copy();c[11]=matrix[1].copy()
            c[9]=np.array([matrix[0,3],matrix[1,3],matrix[2,3],0])
            c[10][3]=0;c[11][3]=0
            if radial:c[9][:2]-=.5
            output,_=execute(program,streams,c)
            position=streams[0] if address=='0x81875470' else streams[1]
            model=np.array([np.dot(position[:2],c[0][:2])+c[0][3]+literal[2],
                np.dot(position[:2],c[1][:2])+c[1][3]+literal[2],c[8][0],
                np.dot(position[:2],c[3][:2])+c[3][3]])
            expected_clip=np.array([np.dot(model,c[index]) for index in range(4,8)])
            expected_uv=matrix[:2]@np.array([*streams[0][:2],0,1])-(.5 if radial else 0)
            if address=='0x818756e0':expected_uv=streams[0][:2]
            maximum_clip=max(maximum_clip,float(np.max(np.abs(output[62]-expected_clip))))
            maximum_uv=max(maximum_uv,float(np.max(np.abs(output[0][:2]-expected_uv))))
            if address=='0x818756e0':
                assert np.allclose(output[1][:2],streams[0][:2]-c[9][:2],atol=1e-12,rtol=0)
            else:
                expected_phase=2*np.linalg.norm(expected_uv) if radial else expected_uv[0]
                actual_phase=2*np.linalg.norm(output[0][:2]) if radial else output[0][0]
                maximum_phase=max(maximum_phase,float(abs(actual_phase-expected_phase)))
        assert max(maximum_clip,maximum_uv,maximum_phase)<1e-10
        records.append(dict(address=address,source=source,constants=constant_table(container),program=program,
            supplied_constant_probe={str(k):v.tolist() for k,v in constants.items()},outputs={str(k):[None if not np.isfinite(x) else float(x) for x in v] for k,v in outputs.items()},trace=trace,
            numeric_checks=dict(samples=512,clip_max_error=maximum_clip,uv_max_error=maximum_uv,phase_max_error=maximum_phase,
                zero_scale_cases=128,radial_upload_offset_cases=256,literal_values=literal.tolist(),literal_source_offset='0x34',
                scope='Supplied raw uploaded rows and constants; CPU matrix-copy/upload behavior is separately evidenced by original disassembly, not executed end-to-end.'),
            limits=['Supplied post-declaration fetch values; raw format/slot placeholders are not emulated.',
                'Float64 algebra probe, not GPU floating-point, interpolation or vertex declaration precision parity.',
                'c255 supplied from retained literal bytes; verify_xui_literal_upload.py separately executes the bounded original vertex-bind path to that register.']))
    comparison=[]
    for opcode in (5,7):
        instruction=microcode.alu((0x000f8000,0,(opcode<<24)|0x8000ff00))
        _,result=microcode.execute(dict(instructions=[instruction]),(-1,1))
        expected=[0,1,0,0] if opcode==5 else [1,1,0,0]
        assert np.array_equal(result,expected)
        comparison.append(dict(opcode=opcode,name=instruction['vector_name'],negative_positive_zero_result=result.tolist()))
    report=dict(vertex_shaders=records,comparison_opcode_regression=comparison,original_bytes_retained=True,renderer_changed=False)
    (a.output/'vertex-verification.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(vertex_shaders=len(records),comparison_checks=comparison,outputs=[r['outputs'] for r in records])))

if __name__=='__main__':main()
