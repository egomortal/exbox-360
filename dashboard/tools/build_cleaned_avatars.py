#!/usr/bin/env python3
"""Build the practical four-avatar cleanup, optionally repeating saved repairs."""
import argparse,json,subprocess,sys
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent)
    p.add_argument('--output',type=Path)
    p.add_argument('--rebake',action='store_true')
    a=p.parse_args();root=a.root.resolve();assets=root/'private/avatar/multiview/cleaned'
    def run(*args):subprocess.run(list(map(str,args)),check=True)
    if a.rebake:
        for e in json.loads((assets/'manifest.json').read_text())['avatars']:
            run(sys.executable,assets/'project_surface_repairs.py',assets/e['obj'],root/e['views'],assets/e['texture'],'--tools',root/'tools','--helpers',root/'private/avatar/multiview','--head-start',e['head_start'],'--head-baseline',root/e['head_baseline'],'--priority-regions',assets/e['controls'],*e['projection_flags'])
    command=[sys.executable,root/'tools/build_bayonetta_image_models.py','--root',root]
    if a.output:command+=['--output',a.output]
    run(*command)
if __name__=='__main__':main()