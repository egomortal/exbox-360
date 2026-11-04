import os
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1';os.environ['MKL_NUM_THREADS']='1'
from pathlib import Path
import numpy as np,subprocess,tempfile,hashlib,json,time
O=Path(__file__).resolve().parent;weights=np.load(O/'model/reference-weights.npz',allow_pickle=False)
rgb=np.arange(5*7*3,dtype=np.uint8).reshape(5,7,3)*2
x=rgb.astype(np.float32).transpose(2,0,1)/np.float32(255);original=x.copy()
for index in range(67):
 w=weights[f'body.{index}.weight']
 if index%2==0:
  padded=np.pad(x,((0,0),(1,1),(1,1)));windows=np.lib.stride_tricks.sliding_window_view(padded,(3,3),axis=(1,2));columns=windows.transpose(0,3,4,1,2).reshape(-1,35)
  x=(w.reshape(w.shape[0],-1)@columns+weights[f'body.{index}.bias'][:,None]).reshape(w.shape[0],5,7)
 else:x=np.where(x>=0,x,x*w[:,None,None])
x=x.reshape(3,4,4,5,7).transpose(0,3,1,4,2).reshape(3,20,28)+np.repeat(np.repeat(original,4,axis=1),4,axis=2)
expected=np.floor(np.clip(x.transpose(1,2,0),0,1)*255+0.5).astype(np.uint8)
with tempfile.TemporaryDirectory(prefix='exbox-x17-reference-') as work:
 d=Path(work);(d/'input.rgb').write_bytes(rgb.tobytes());outputs=[]
 for n in range(2):
  command=[str(O/'cpu-infer'),str(O/'model/generic-x4.param'),str(O/'model/generic-x4.bin'),str(d/'input.rgb'),str(d/f'output{n}.rgb'),'7','5'];p=subprocess.run(command,capture_output=True,text=True);assert not p.returncode,(p.returncode,p.stderr);outputs.append((d/f'output{n}.rgb').read_bytes())
 assert outputs[0]==outputs[1]
 actual=np.frombuffer(outputs[0],np.uint8).reshape(expected.shape);error=np.abs(actual.astype(int)-expected.astype(int));assert error.max()<=1,int(error.max())
 result=dict(numpy_reference_max_byte_error=int(error.max()),differing_channels=int(np.count_nonzero(error)),deterministic_same_processes=True,output_sha256=hashlib.sha256(outputs[0]).hexdigest(),shape=list(actual.shape),model_modified=False)
 (O/'inference-validation.json').write_text(json.dumps(result,indent=2));print(result)
