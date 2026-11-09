"""Generate headers locally from user HLSL. SPDX-License-Identifier: GPL-3.0-or-later"""
from pathlib import Path
import hashlib,json,subprocess
def sha(data):return hashlib.sha256(data).hexdigest()
def build(source,tools,output,contract):
 source=Path(source).resolve(strict=True);tools=Path(tools).resolve(strict=True);output=Path(output).resolve()
 if output.exists():raise ValueError('Shader output must be fresh')
 if output==source or output.is_relative_to(source) or source.is_relative_to(output):raise ValueError('Keep generated files separate from originals')
 inputs={}
 for name,row in contract['sources'].items():
  if Path(name).name!=name:raise ValueError('Invalid source name')
  raw=(source/name).read_bytes()
  if sha(raw) not in row['input_sha256']:raise ValueError('Unsupported shader: '+name)
  inputs[name]=raw.decode().replace('\r\n','\n')
 executables={name:tools/name for name in ['glslangValidator','spirv-val','spirv-dis']}
 if not all(p.is_file() for p in executables.values()):raise ValueError('Supply glslangValidator, spirv-val and spirv-dis')
 output.mkdir(parents=True);headers=output/'gpu/shaders';headers.mkdir(parents=True);records=[]
 for name,text in inputs.items():
  stem=Path(name).stem;text=text.replace('ConstantBuffer<Constants> g_constants','[[vk::push_constant]] ConstantBuffer<Constants> g_constants').replace('cbuffer ResizeConstants','[[vk::push_constant]] cbuffer ResizeConstants').replace(': register(b0, space1)','');adapted=output/name;adapted.write_text(text,encoding='utf-8',newline='\n')
  declarations=['// Original HLSL converted by compile_av2_spirv.py; see manifest.json.\n#pragma once\n#include <cstdint>\nnamespace ae::gpu::shaders {']
  for row in contract['entries']:
   if row['source']!=name:continue
   if Path(row['filename']).name!=row['filename']:raise ValueError('Invalid generated shader filename')
   target=output/row['filename'];argv=[str(executables['glslangValidator']),'-D','-V','--target-env','vulkan1.2','--hlsl-offsets','--auto-map-locations','-S',row['stage'],'-e',row['entry'],'-o',str(target),str(adapted)]
   if row['stage']=='vert':argv.insert(1,'--invert-y')
   subprocess.run(argv,check=True,cwd=output);subprocess.run([str(executables['spirv-val']),'--target-env','vulkan1.2',str(target)],check=True,cwd=output);dis=subprocess.check_output([str(executables['spirv-dis']),str(target)],text=True,cwd=output);data=target.read_bytes()
   if sha(data)!=row['sha256']:raise ValueError('Shader output differs: '+row['filename'])
   declarations.append('alignas(4) inline constexpr uint8_t k'+stem.capitalize()+'_'+row['tag']+'_spirv[] = {'+','.join(str(x) for x in data)+'};');records.append(dict(output=row['filename'],sha256=sha(data),validated=True,push_constant_present='PushConstant' in dis))
  declarations.append('}\n');data='\n'.join(declarations).encode();header=stem+'_shaders.h'
  if sha(data)!=contract['headers'][header]:raise ValueError('Header differs: '+header)
  (headers/header).write_bytes(data)
 (output/'manifest.json').write_text(json.dumps(dict(status='PASS',entries=records),indent=2)+'\n');return records
