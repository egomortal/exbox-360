# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin a locally source-built runtime; never downloads or executes a new program."""
from pathlib import Path
import hashlib,json,platform
import PIL,numpy
O=Path(__file__).resolve().parent
EXPECTED={'generic-x4.bin':'bfdfaebf0b26be9442faa30b8cec617fe04a808ae2aa3616ece85b92d81ae0ce','generic-x4.param':'ff5d1e61da61a8e581c9262dc41cc7a446d3866e49f11706d637ec1c632aedc7'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 rows=json.loads((O/'adapter-build.json').read_text());assert rows and all(r['exit']==0 for r in rows)
 for name,digest in EXPECTED.items():assert sha(O/'model'/name)==digest,name
 result={'schema':1,'profile':'ncnn-cpu-fp32-singlethread-v1','ncnn_commit':'56775de50990ab7f16627efdcf5529b49541206f','model_release':'Real-ESRGAN v0.2.5.0 realesr-general-x4v3','adapter_sha256':sha(O/'cpu-infer'),'adapter_source_sha256':sha(O/'cpu_infer.cpp'),'models':EXPECTED,'pillow':PIL.__version__,'numpy':numpy.__version__,'python':platform.python_version(),'architecture':platform.machine(),'determinism_scope':'Same pinned runtime, architecture, library versions, input bytes and recipe; not a cross-version or cross-CPU guarantee'}
 (O/'runtime-receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
