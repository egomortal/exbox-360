from pathlib import Path
import tarfile,re,subprocess,json,shutil,hashlib
O=Path(__file__).resolve().parent;A=O/'upstream/ncnn-source.tar.gz';T=O/'build-work';T.mkdir(exist_ok=True)
with tarfile.open(A) as archive:archive.extractall(T,filter='data')
S=T/'ncnn-56775de50990ab7f16627efdcf5529b49541206f';B=T/'build';B.mkdir(exist_ok=True)
keep={'Input','Convolution','PReLU','PixelShuffle','Interp','BinaryOp','Split','Padding','Crop','Packing','Cast','InnerProduct','ReLU','Gemm','Flatten'}
cmd=['cmake','-S',str(S),'-B',str(B),'-DCMAKE_BUILD_TYPE=Release','-DNCNN_VULKAN=OFF','-DNCNN_OPENMP=OFF','-DNCNN_RUNTIME_CPU=OFF','-DNCNN_BUILD_TOOLS=OFF','-DNCNN_BUILD_EXAMPLES=OFF','-DNCNN_BUILD_BENCHMARK=OFF','-DNCNN_BUILD_TESTS=OFF','-DNCNN_AVX512=OFF','-DNCNN_AVX2=OFF','-DNCNN_AVX=OFF','-DNCNN_C_API=OFF','-DNCNN_INT8=OFF']
for layer in re.findall(r'ncnn_add_layer\((\w+)',(S/'src/CMakeLists.txt').read_text()):cmd.append('-DWITH_LAYER_'+layer.lower()+'='+('ON' if layer in keep else 'OFF'))
rows=[]
for name,command in [('configure',cmd),('compile',['cmake','--build',str(B),'--parallel','4'])]:
 p=subprocess.run(command,capture_output=True,text=True);(O/(name+'.log')).write_text(p.stdout+p.stderr);rows.append(dict(name=name,exit=p.returncode,command=command));(O/'runtime-build.json').write_text(json.dumps(rows,indent=2));print(name,p.returncode,flush=True);assert not p.returncode,p.stderr[-2000:]
(O/'runtime-build-path.txt').write_text(str(B));print('CPU runtime built from source',flush=True)

