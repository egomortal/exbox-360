"""Record original sidebar dimensions and animation properties without guessing transforms."""
from pathlib import Path
import argparse, hashlib, json, re
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
root=ET.parse(args.source).getroot()
nodes=[];timelines=[]
for element in root.iter():
    if element.tag=='XuiText':
        props=element.find('Properties')
        if props is not None and re.fullmatch(r'blade_\d_txt',props.findtext('Id') or ''):
            nodes.append({p.tag:p.text for p in props})
    elif element.tag=='Timeline' and re.fullmatch(r'blade_\d_txt',element.findtext('Id') or ''):
        names=[p.text for p in element.findall('TimelineProp')]
        timelines.append(dict(id=element.findtext('Id'),properties=names,
            frames=[dict(frame=int(k.findtext('Time')),interpolation=k.findtext('Interpolation'),
                values=dict(zip(names,[p.text for p in k.findall('Prop')]))) for k in element.findall('KeyFrame')]))
report=dict(source=str(args.source),source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),
    scope='Original source units and transforms, not final native screen coordinates',
    root_properties={p.tag:p.text for p in root.find('Properties')},nodes=nodes,timelines=timelines)
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(f'{len(nodes)} original text rectangles, {len(timelines)} property timelines recorded')
