"""Generate source-derived headers. SPDX-License-Identifier: GPL-3.0-or-later"""
from pathlib import Path
import argparse,hashlib,json,re
XR_SOURCES=['constant_table.h','dxc_compiler.cpp','dxc_compiler.h','pch.h','shader.h','shader_code.h','shader_common.h','shader_recompiler.cpp','shader_recompiler.h','ucode_fingerprint.cpp','ucode_fingerprint.h','xenosrecomp.h','xenosrecomp_core.cpp']
def xr_header(source):
 source=Path(source)
 crlf={'constant_table.h','pch.h','shader.h','shader_code.h','shader_common.h','shader_recompiler.h','xenosrecomp.h','xenosrecomp_core.cpp','BASE'}
 def data(p):
  raw=Path(p).read_bytes().replace(b'\r\n',b'\n')
  return raw.replace(b'\n',b'\r\n') if Path(p).name in crlf else raw
 text=''.join(name+':'+data(source/name).hex()+';' for name in XR_SOURCES);fingerprint=hashlib.sha256(text.encode()).hexdigest()[:16]
 def array(p):return ''.join('0x%02x,'%b for b in data(p))
 value='#pragma once\n\n#include <cstdint>\n\n'
 value+='inline constexpr uint64_t kXrTranslatorSourceHash = 0x'+fingerprint+'ull;\n'
 value+='inline constexpr unsigned char kXrCommonHeader[] = { '+array(source/'rexglue_shader_common.h')+' };\n'
 value+='inline constexpr unsigned char kXrBaseFile[] = { '+array(source.parent/'BASE')+' };\n'
 return value.encode(),fingerprint
def ffmpeg_config(source):
 value=Path(source).read_text();changes=['HAVE_ASM_TYPES_H','HAVE_LINUX_PERF_EVENT_H','HAVE_MALLOC_H','HAVE_MEMALIGN','HAVE_SCHED_GETAFFINITY']
 for name in changes:
  value,n=re.subn(r'^#define '+name+r' \d+$','#define '+name+' 0',value,flags=re.M)
  if n!=1:raise ValueError('Expected exactly one FFmpeg feature declaration: '+name)
 return value.encode()
def ffmpeg_headers(source):
 # config.h is supplied exclusively by the reviewed ffmpeg-glue manifest.
 return {'config_ps5_x86_64.h':ffmpeg_config(source)}
def fresh_write(path,data):
 path=Path(path)
 if path.exists() and path.read_bytes()!=data:raise ValueError('Refuse differing existing generated header: '+str(path))
 path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('mode',choices=['xenos-abi','ffmpeg-config']);p.add_argument('--source',required=True);p.add_argument('--output',required=True);a=p.parse_args()
 data,fp=xr_header(a.source) if a.mode=='xenos-abi' else (ffmpeg_config(a.source),None);fresh_write(a.output,data);print(json.dumps(dict(output=a.output,sha256=hashlib.sha256(data).hexdigest(),translator_fingerprint=fp)))
