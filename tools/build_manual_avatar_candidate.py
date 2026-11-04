#!/usr/bin/env python3
"""Rebuild the unfinished, manually bounded avatar refinement candidate."""
import argparse,json,shutil,subprocess,sys,tempfile
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--refine',action='store_true',help='recreate bounded mesh edits and rebake')
    parser.add_argument('--rebake',action='store_true')
    args=parser.parse_args();root=args.root.resolve()
    asset=root/'private/avatar/multiview/manual-refinement'
    entries=json.loads((asset/'manifest.json').read_text())['avatars']
    output=args.output or root/'private/avatar/kit360.manual-candidate.nxak'
    if output.resolve()==(root/'private/avatar/kit360.nxak').resolve():parser.error('Unfinished candidates cannot replace the active pack through this tool')
    def run(*a):subprocess.run(list(map(str,a)),check=True)
    for entry in entries:
        if args.refine:
            run(sys.executable,root/'tools/refine_avatar_geometry.py',root/entry['original_obj'],asset/entry['obj'],'--head-start',entry['head_start'],'--controls',asset/entry['controls'])
        if args.rebake or args.refine:
            run(sys.executable,asset/'project_manual_regions.py',asset/entry['obj'],root/entry['views'],asset/entry['texture'],'--tools',root/'tools','--helpers',root/'private/avatar/multiview','--head-start',entry['head_start'],'--head-baseline',root/entry['head_baseline'],'--priority-regions',asset/entry['controls'],*entry['projection_flags'])
    with tempfile.TemporaryDirectory(prefix='avatar-manual-') as temporary:
        previous=Path(temporary)/'base.nxak'
        run(sys.executable,root/'tools/expand_bayonetta.py',root/'private/avatar/kit360.base.nxak',previous)
        for i,entry in enumerate(entries):
            next_pack=Path(temporary)/f'kit-{i}.nxak'
            run(sys.executable,root/'tools/import_image_model.py',previous,asset/entry['obj'],asset/entry['texture'],next_pack,'--label',entry['label'],'--height',entry['height'],'--mirror-x')
            previous=next_pack
        output.parent.mkdir(parents=True,exist_ok=True)
        pending=output.with_suffix('.building');shutil.copyfile(previous,pending);pending.replace(output)
    print(output)
if __name__=='__main__':main()