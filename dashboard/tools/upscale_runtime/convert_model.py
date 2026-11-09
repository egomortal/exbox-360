# Original Ex-Box adapter: convert the pinned generic weights as data, never execute pickle globals.
from pathlib import Path
import pickle,zipfile,io,collections,struct,hashlib,json
import numpy as np
O=Path(__file__).resolve().parent;P=O/'upstream/models/realesr-general-x4v3.pth';D=O/'model';D.mkdir(exist_ok=True)
assert hashlib.sha256(P.read_bytes()).hexdigest()=='8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292','Pinned model input changed'
class Storage:pass
class Reader(pickle.Unpickler):
 def __init__(self,data,archive,prefix):super().__init__(io.BytesIO(data));self.archive=archive;self.prefix=prefix
 def find_class(self,module,name):
  if (module,name)==('collections','OrderedDict'):return collections.OrderedDict
  if (module,name)==('torch','FloatStorage'):return Storage
  if (module,name)==('torch._utils','_rebuild_tensor_v2'):return self.tensor
  raise ValueError('unsupported model object '+module+'.'+name)
 def persistent_load(self,key):
  assert isinstance(key,tuple) and len(key)==5 and key[0]=='storage' and key[1] is Storage
  assert str(key[2]).isdigit() and 0<=key[4]<=2000000
  data=self.archive.read(self.prefix+'data/'+str(key[2]));assert len(data)==key[4]*4
  return np.frombuffer(data,dtype='<f4')
 @staticmethod
 def tensor(storage,offset,shape,strides,requires_grad,hooks):
  assert not requires_grad and len(shape)==len(strides) and len(shape)<=4 and offset>=0
  assert all(0<x<=2000000 for x in shape) and all(x>=0 for x in strides)
  last=offset+sum((s-1)*t for s,t in zip(shape,strides));assert last<len(storage)
  return np.ndarray(shape,dtype='<f4',buffer=storage,offset=offset*4,strides=tuple(s*4 for s in strides)).copy()
with zipfile.ZipFile(P) as archive:
 assert sum(info.file_size for info in archive.infolist())<16000000
 names=[n for n in archive.namelist() if n.endswith('/data.pkl')];assert len(names)==1
 data=Reader(archive.read(names[0]),archive,names[0][:-8]).load()
weights=data.get('params_ema',data.get('params'));assert isinstance(weights,dict)
assert len(weights)==101 and all(np.isfinite(v).all() for v in weights.values())
layers=[];blobs=set();binary=bytearray()
def add(kind,name,inputs,outputs,params=''):
 layers.append(' '.join([kind,name,str(len(inputs)),str(len(outputs)),*inputs,*outputs,params]).strip());blobs.update(outputs)
add('Input','image',[],['input']);add('Split','branches',['input'],['data','base'])
current='data'
for index in range(67):
 weight=weights[f'body.{index}.weight'];out=f'layer{index}'
 if index%2==0:
  bias=weights[f'body.{index}.bias'];assert weight.ndim==4 and weight.shape[2:]==(3,3) and len(bias)==weight.shape[0]
  assert weight.shape[1]==(3 if index==0 else 64) and weight.shape[0]==(48 if index==66 else 64)
  add('Convolution',f'conv{index}',[current],[out],f'0={len(bias)} 1=3 2=1 3=1 4=1 5=1 6={weight.size}')
  binary+=struct.pack('<I',0)+weight.astype('<f4').tobytes()+bias.astype('<f4').tobytes()
 else:
  assert weight.shape==(64,);add('PReLU',f'prelu{index}',[current],[out],'0=64');binary+=weight.astype('<f4').tobytes()
 current=out
add('PixelShuffle','shuffle',[current],['upscaled'],'0=4 1=0');add('Interp','nearest',['base'],['residual'],'0=1 1=4.0 2=4.0');add('BinaryOp','sum',['upscaled','residual'],['output'],'0=0')
(D/'generic-x4.param').write_text('7767517\n'+str(len(layers))+' '+str(len(blobs))+'\n'+'\n'.join(layers)+'\n');(D/'generic-x4.bin').write_bytes(binary)
np.savez(D/'reference-weights.npz',**weights)
receipt=dict(source_url='https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth',source_sha256=hashlib.sha256(P.read_bytes()).hexdigest(),architecture='SRVGGNetCompact 3/3 channels,64 features,32 body convolutions,4x,PReLU,nearest residual',training='None; unmodified generic upstream weights',inference='CPU float32 single thread; fixed converted parameter/weight files',files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in D.iterdir() if p.is_file()})
(O/'model-receipt.json').write_text(json.dumps(receipt,indent=2));print('Converted',len(weights),'tensors',len(layers),'layers',flush=True)
