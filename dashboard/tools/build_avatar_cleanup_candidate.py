#!/usr/bin/env python3
"""Build the saved cleanup candidate; visual finishing is incomplete."""
import argparse,json,subprocess,sys,tempfile,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent)
p.add_argument('--output',type=Path)
p.add_argument('--rebake',action='store_true')
a=p.parse_args();r=a.root.resolve();m=r/'private/avatar/multiview';assets=m/'finishing-cleanup-candidates'
output=a.output or r/'private/avatar/kit360.cleanup-candidate.nxak'
if output.resolve()==(r/'private/avatar/kit360.nxak').resolve():
 p.error('This unfinished candidate must not overwrite the active reviewed pack')
entries=json.loads((assets/'manifest.json').read_text())['avatars']
def run(*args):subprocess.run(list(map(str,args)),check=True)
if a.rebake:
 for e in entries:
  run(sys.executable,assets/'project_preserved_head.py',r/e['obj'],assets/e['views'],assets/e['texture'],'--tools',r/'tools','--helpers',m,'--head-start',e['head_start'],'--head-baseline',assets/e['head_baseline'],'--priority-regions',assets/e['priority_regions'],*e['projection_flags'])
with tempfile.TemporaryDirectory(prefix='avatar-cleanup-') as temp:
 prev=Path(temp)/'base.nxak';run(sys.executable,r/'tools/expand_bayonetta.py',r/'private/avatar/kit360.base.nxak',prev)
 for i,e in enumerate(entries):
  nxt=Path(temp)/f'kit-{i}.nxak'
  run(sys.executable,r/'tools/import_image_model.py',prev,r/e['obj'],assets/e['texture'],nxt,'--label',e['label'],'--height',e['height'],'--mirror-x')
  prev=nxt
 output.parent.mkdir(parents=True,exist_ok=True);pending=output.with_suffix('.building');shutil.copyfile(prev,pending);pending.replace(output)
print(output)
