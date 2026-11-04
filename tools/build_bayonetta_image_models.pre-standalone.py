#!/usr/bin/env python3
"""Build the four-avatar pack with continuous surfaces or historical assets."""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    p.add_argument('--output', type=Path)
    mode = p.add_mutually_exclusive_group()
    mode.add_argument('--pre-cleanup', action='store_true', help='rebuild the earlier continuous pack before practical cleanup')
    mode.add_argument('--legacy', action='store_true', help='use original single-image meshes and traced fronts')
    mode.add_argument('--traced-front', action='store_true', help='rebuild the earlier multiview pack with flat front overlays')
    a = p.parse_args()
    root = a.root.resolve(); tools = root/'tools'; avatar = root/'private/avatar'
    output = a.output or avatar/'kit360.nxak'
    output.parent.mkdir(parents=True, exist_ok=True)
    variants = []
    if not (a.legacy or a.traced_front):
        assets = avatar/'multiview'/('continuous' if a.pre_cleanup else 'cleaned')
        config = json.loads((assets/'manifest.json').read_text())
        for entry in config['avatars']:
            extra = ['--height',str(entry['height'])]
            if entry.get('mirror_x'):
                extra.append('--mirror-x')
            variants.append((entry['label'],assets/entry['obj'],assets/entry['texture'],extra))
    else:
        names = [('Bayonetta 1','bayonetta1','b1-mv-profile'),
                 ('Bayonetta 2','bayonetta2','b2-mv'),
                 ('Bayonetta 3','bayonetta3','b3-mv'),
                 ('Young Cereza + Cheshire','young-cereza-cheshire','cereza-mv')]
        for index,(label,prefix,stem) in enumerate(names):
            if a.legacy:
                obj = avatar/(prefix+'-image-model'+('' if index==0 else '-source')+'.obj')
                texture = avatar/(prefix+'-image-model-texture.png')
            else:
                obj = avatar/'multiview'/(stem+'-import.obj')
                texture = avatar/'multiview'/(stem+'-import.png')
            extra = ['--front-trace',str(avatar/(prefix+'-front-trace.png'))]
            if index==0:
                extra += ['--face-detail',str(avatar/'bayonetta1-face-detail.png')]
            if index==3:
                extra += ['--height','1.15'] + (['--cheshire'] if a.legacy else [])
            variants.append((label+' (image model)',obj,texture,extra))
    if len(variants)!=4:
        raise ValueError('The expansion requires exactly four avatars')
    for _,obj,texture,_ in variants:
        if not obj.is_file() or not texture.is_file():
            raise FileNotFoundError(f'Missing avatar asset: {obj} / {texture}')
    with tempfile.TemporaryDirectory(prefix='bayo-kit-') as temp:
        previous=Path(temp)/'expanded.nxak'
        subprocess.run([sys.executable,str(tools/'expand_bayonetta.py'),str(avatar/'kit360.base.nxak'),str(previous)],check=True)
        for index,(label,obj,texture,extra) in enumerate(variants):
            next_kit=Path(temp)/f'kit-{index}.nxak'
            subprocess.run([sys.executable,str(tools/'import_image_model.py'),str(previous),str(obj),str(texture),str(next_kit),'--label',label,*extra],check=True)
            previous=next_kit
        # Only replace the destination once the whole pack has built.
        import shutil
        pending=output.with_name(output.name+'.building')
        shutil.copyfile(previous,pending)
        pending.replace(output)
    print(f'Four image models ready: {output}')


if __name__=='__main__':
    main()
