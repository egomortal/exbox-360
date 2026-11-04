"""Publish a local review gallery from the native original-XUI captures."""
from pathlib import Path
import argparse,json,shutil
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--captures',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
for source in a.captures.resolve().rglob('*'):
    if source.is_file() and source.suffix in {'.png','.webp','.json','.log','.csv'}:
        target=out/source.relative_to(a.captures.resolve());target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
for name in ['XUI_RUNTIME.md','CODE_MAP.md','BLADES_SOURCE_DATA.md']:
    shutil.copy2(root/'docs'/name,out/name)
shutil.copy2(root.parent/'era-reference/audit-20261025/designer-blades-games.jpg',out/'reference-games.jpg')
html=(root/'tools/xui_review_template.html').read_text(encoding='utf-8').replace('__RECORDS__',json.dumps(json.loads((out/'captures.json').read_text()),separators=(',',':')).replace('</','<\\/'))
(out/'index.html').write_text(html,encoding='utf-8');print(out/'index.html')
