# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a source-built local image engine under the user's data folder."""
from pathlib import Path
import argparse,json,shutil,subprocess,sys,hashlib
from upscale import source_roots,UpscaleError
O=Path(__file__).resolve().parent

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--data-root',required=True);parser.add_argument('--build',action='store_true',help='Fetch pinned official source/model data and compile locally; otherwise print the plan');args=parser.parse_args();data=Path(args.data_root).expanduser().resolve();target=data/'upscale-runtime'
 if target.is_symlink() or any(target.resolve().is_relative_to(p) for p in source_roots()):raise UpscaleError('Choose a user data folder outside the source checkout.')
 names=['fetch_sources.py','build_runtime.py','build_adapter.py','validate_inference.py','pin_runtime.py'];commands=[[sys.executable,str(target/name)] for name in names]
 if not args.build:print(json.dumps({'status':'plan','destination':str(target),'commands':commands,'requirements':'Python3.12, Pillow/NumPy, CMake, C++17 compiler; no sudo or downloaded programs'},indent=2));return 0
 target.mkdir(parents=True,exist_ok=True)
 for source in (O/'upscale_runtime').iterdir():
  if not source.is_file():continue
  dest=target/source.name
  if dest.exists() and dest.read_bytes()!=source.read_bytes():raise UpscaleError('Existing engine source differs; choose a new data folder or review the local changes.')
  if not dest.exists():shutil.copy2(source,dest)
 rows=[]
 for cmd in commands:
  r=subprocess.run(cmd,capture_output=True,text=True,timeout=3600);rows.append({'command':cmd,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr});(target/'setup-results.json').write_text(json.dumps(rows,indent=2))
  if r.returncode:print(json.dumps({'status':'error','message':'Local image engine build failed; see '+str(target/'setup-results.json')}));return 2
 print(json.dumps({'status':'ready','runtime':str(target),'receipt_sha256':hashlib.sha256((target/'runtime-receipt.json').read_bytes()).hexdigest()},indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
