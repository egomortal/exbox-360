"""Execute the bounded original scalar vertex-bind path to verify literal destinations."""
from pathlib import Path
import argparse, collections, hashlib, json, re, struct
from audit_original_runtime import Image
from verify_brush_matrices import Leaf

class Bind(Leaf):
    def __init__(self,image,header):
        super().__init__(image)
        self.device=0x10000000;self.resource=0x20000000
        self.allowed=[(0xf000,0x12000),(self.device,self.device+0x7000),(self.resource,self.resource+0x250+len(header))]
        self.r[3]=self.device;self.r[4]=self.resource;self.cr={};self.copies=[]
        self.write(self.resource+0x250,header);self.write(self.resource+4,(1).to_bytes(4,'big'))
    def write(self,address,data):
        if not any(start<=address and address+len(data)<=end for start,end in self.allowed):raise ValueError('Write outside supplied bind state')
        super().write(address,data)
    def read(self,address,count):
        if not any(start<=address and address+count<=end for start,end in self.allowed):raise ValueError('Read outside supplied bind state')
        return super().read(address,count)
    def run_bind(self):
        instructions={ins.address:ins for ins in self.image.cs.disasm(self.image.data[0x818edee8-self.image.base:0x818ee040-self.image.base],0x818edee8)}
        pc=0x818edee8
        def register(text):return int(text[1:])
        def address(text):
            m=re.fullmatch(r'(-?0x[0-9a-f]+|-?\d+)\((r\d+)\)',text)
            if not m:raise ValueError('Unsupported address '+text)
            return (self.r[register(m[2])]+int(m[1],0))&0xffffffff
        for _ in range(1024):
            ins=instructions[pc];op=ins.mnemonic;args=ins.op_str.split(', ');next_pc=pc+4
            self.counts[op]+=1
            if op=='mflr':self.r[register(args[0])]=0
            elif op=='stwu':
                target=address(args[1]);self.write(target,(self.r[register(args[0])]&0xffffffff).to_bytes(4,'big'));self.r[1]=target
            elif op=='mr':self.r[register(args[0])]=self.r[register(args[1])]
            elif op in ('li','addi','addic.'):
                value=int(args[-1],0)+(0 if op=='li' else self.r[register(args[1])]);self.r[register(args[0])]=value&0xffffffff
                if op.endswith('.'):self.cr['cr0']=0 if value==0 else -1 if value<0 else 1
            elif op=='add':self.r[register(args[0])]=(self.r[register(args[1])]+self.r[register(args[2])])&0xffffffff
            elif op in ('lwz','ld','lhz','lbz'):
                count={'lwz':4,'ld':8,'lhz':2,'lbz':1}[op];self.r[register(args[0])]=int.from_bytes(self.read(address(args[1]),count),'big')
            elif op in ('stw','std','stb'):
                count={'stw':4,'std':8,'stb':1}[op];self.write(address(args[1]),(self.r[register(args[0])] & ((1<<(count*8))-1)).to_bytes(count,'big'))
            elif op in ('or','and'):
                x=self.r[register(args[1])];y=self.r[register(args[2])];self.r[register(args[0])]=x|y if op=='or' else x&y
            elif op in ('ori','oris'):
                self.r[register(args[0])]=self.r[register(args[1])]|(int(args[2],0)<<(16 if op=='oris' else 0))
            elif op=='clrlwi':self.r[register(args[0])]=self.r[register(args[1])]&((1<<(32-int(args[2],0)))-1)
            elif op=='slwi':self.r[register(args[0])]=(self.r[register(args[1])]<<int(args[2],0))&0xffffffff
            elif op=='rlwinm':
                value=self.r[register(args[1])]&0xffffffff;shift,begin,end=map(lambda x:int(x,0),args[2:]);value=((value<<shift)|(value>>((32-shift)%32)))&0xffffffff
                mask=sum(1<<(31-bit) for bit in range(32) if (begin<=bit<=end if begin<=end else bit>=begin or bit<=end));self.r[register(args[0])]=value&mask
            elif op in ('cmplwi','cmpldi','cmplw'):
                field=args[0] if args[0].startswith('cr') else 'cr0';values=args[1:] if field!='cr0' else args
                left=self.r[register(values[0])];right=self.r[register(values[1])] if op=='cmplw' else int(values[1],0)
                self.cr[field]=(left>right)-(left<right)
            elif op in ('beq','bne','bge','blt'):
                field=args[0] if len(args)==2 else 'cr0';value=self.cr[field]
                condition={'beq':value==0,'bne':value!=0,'bge':value>=0,'blt':value<0}[op]
                if condition:next_pc=int(args[-1],0)
            elif op=='bl':
                target=int(args[0],0)
                if target==0x8195ece0:
                    destination,source,count=self.r[3:6];self.write(destination,self.read(source,count));self.copies.append(dict(source=hex(source-self.resource-0x250),device_offset=hex(destination-self.device),bytes=count))
                elif target!=0x8195ec68:raise ValueError('Unimplemented external call '+hex(target))
            elif op=='b' and int(args[0],0)==0x8195ecb8:return
            else:raise ValueError(f'Unsupported instruction {pc:08x}: {op} {ins.op_str}')
            pc=next_pc
        raise ValueError('Bind instruction limit reached')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    image=Image(a.assets/'runtime-verification-20261025','xam');recovery=json.loads((a.assets/'blades-gradient-texture-20261026/shader-recovery.json').read_text());checks=[]
    for source in recovery['shaders']:
        if source['image']!='xam' or source['source_address'] not in ('0x81875470','0x818756e0'):continue
        path=a.assets/'blades-gradient-texture-20261026/original-shaders'/(source['container_sha256']+'.xshader');raw=path.read_bytes();assert hashlib.sha256(raw).hexdigest()==source['container_sha256']
        machine=Bind(image,raw[:source['code_offset']]);machine.run_bind()
        assert len(machine.copies)==1 and machine.copies[0]['device_offset']=='0x1770'
        data=machine.read(machine.device+0x1770,16);assert data==raw[0x34:0x44]
        checks.append(dict(shader=source['source_address'],copies=machine.copies,literal_values=list(struct.unpack('>4f',data)),vertex_constant_register=(0x1770-0x780)//16,opcodes=dict(machine.counts),source_shader_sha256=source['container_sha256']))
    assert len(checks)==2
    report=dict(checks=checks,original_bind_function='0x818edee8',original_vertex_upload_base_evidence='0x818ed8d0 computes (register + 0x78) << 4',
        original_image_sha256=hashlib.sha256(image.data).hexdigest(),
        scope='Original scalar vertex-bind instructions executed with bounded supplied device/resource memory; register save/restore skipped and memcpy intrinsic implemented. No original GPU, object allocation, stream patching or host services emulated.')
    (a.output/'literal-upload-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(shaders=len(checks),literal_register=255,checks=[c['literal_values'] for c in checks])))

if __name__=='__main__':main()
