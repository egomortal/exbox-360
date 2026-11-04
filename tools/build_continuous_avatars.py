#!/usr/bin/env python3
"""Rebake saved projection paintings, pack them, and optionally render a review."""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent)
    p.add_argument('--output',type=Path)
    p.add_argument('--rebake',action='store_true')
    p.add_argument('--renderer',type=Path,help='compiled tools/render_image_model.cpp executable')
    p.add_argument('--review-dir',type=Path)
    a=p.parse_args()
    if bool(a.renderer)!=bool(a.review_dir):
        p.error('--renderer and --review-dir must be provided together')
    root=a.root.resolve();tools=root/'tools';mv=root/'private/avatar/multiview';assets=mv/'continuous'
    config=json.loads((assets/'manifest.json').read_text())
    output=a.output or root/'private/avatar/kit360.nxak'
    def run(*args):subprocess.run(list(map(str,args)),check=True)
    if a.rebake:
        for entry in config['avatars']:
            run(sys.executable,mv/'project_uv_texture.py',assets/entry['obj'],assets/entry['views'],assets/entry['texture'],
                '--tools',tools,'--helpers',mv,'--head-start',entry['head_start'],*entry['projection_flags'])
    run(sys.executable,tools/'build_bayonetta_image_models.py','--pre-cleanup','--root',root,'--output',output)
    if a.renderer:
        directories=[]
        for index,entry in enumerate(config['avatars'],1):
            frames=a.review_dir/entry['id'];frames.mkdir(parents=True,exist_ok=True);directories.append(frames)
            run(a.renderer,output,frames,24,index)
            run(sys.executable,tools/'make_avatar_sweep.py',frames,a.review_dir/(entry['id']+'-sweep.png'),
                '--metrics',a.review_dir/(entry['id']+'-metrics.json'))
        run(sys.executable,tools/'make_expansion_review.py',a.review_dir/'four-avatar-review.png',*directories)


if __name__=='__main__':
    main()
