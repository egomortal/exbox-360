from pathlib import Path
import subprocess,json,hashlib
O=Path(__file__).resolve().parent;B=Path((O/'runtime-build-path.txt').read_text());S=B.parent/'ncnn-56775de50990ab7f16627efdcf5529b49541206f';rows=[]
commands=[('convert',['python3',str(O/'convert_model.py')]),('adapter',['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(S/'src'),'-I'+str(B/'src'),str(O/'cpu_infer.cpp'),str(B/'src/libncnn.a'),'-pthread','-o',str(O/'cpu-infer')])]
for name,cmd in commands:
 p=subprocess.run(cmd,capture_output=True,text=True);rows.append(dict(name=name,command=cmd,exit=p.returncode,stdout=p.stdout,stderr=p.stderr));(O/'adapter-build.json').write_text(json.dumps(rows,indent=2));print(name,p.returncode,p.stdout,flush=True);assert not p.returncode,p.stderr[-2500:]
