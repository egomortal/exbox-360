"""Check visible-letter margins from the native layout diagnostic and retain side crops."""
from pathlib import Path
import argparse,csv,json
from PIL import Image,ImageDraw

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve();dest=out/'typography'
rows=[{key:int(value) for key,value in row.items()} for row in csv.DictReader((out/'rail-layout.tsv').open(),delimiter='\t')]
assert len(rows)==41,len(rows)
for row in rows:
    assert min(row['left_gap'],row['right_gap'])>=8,row
    assert abs(row['left_gap']-row['right_gap'])<=7,row
    assert row['top_gap']==(33 if row['active']==row['blade'] else 28),row
    assert row['y']+row['height']<700,row
report=dict(slots_checked=len(rows),all_visible_ink_inside_mapped_faces=True,
    min_side_gap=min(min(r['left_gap'],r['right_gap']) for r in rows),
    max_side_gap_difference=max(abs(r['left_gap']-r['right_gap']) for r in rows),
    scope='Bounds of current reconstructed chrome; not asserted to be original XUI screen coordinates',slots=rows)
(dest/'rail-spacing.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
sheet=Image.new('RGB',(1280,1440),'#24292b');draw=ImageDraw.Draw(sheet)
for index,(name,label,region) in enumerate([
    ('blades-live','Five blades: left stack',(90,155,285,555)),
    ('blades-1888-media','Four blades: left stack',(115,155,365,555)),
    ('blades-1888-live','Right stack',(1500,175,1790,575))]):
    for column,phase in enumerate(['before','after']):
        path=dest/f'rails-before-{name}.png' if phase=='before' else out/f'{name}.png'
        crop=Image.open(path).crop(region)
        crop=crop.resize((int(crop.width*1.1),int(crop.height*1.1)),Image.Resampling.NEAREST)
        x=column*640+12;y=index*480
        draw.text((x,y+10),label+' - '+phase,fill='white')
        sheet.paste(crop,(x,y+34))
sheet.save(dest/'rail-spacing.png')
print(json.dumps({k:v for k,v in report.items() if k!='slots'},indent=2))
