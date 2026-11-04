"""Inventory cached original scene data before making visual estimates."""
from pathlib import Path
from collections import Counter
import argparse,hashlib,json,re
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source_folder',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args();records=[]
for source in sorted(args.source_folder.glob('blades*.xui')):
    root=ET.parse(source).getroot()
    classes=Counter(e.tag for e in root.iter() if e.tag.startswith('Xui'))
    props=Counter(p.tag for group in root.iter('Properties') for p in group)
    timeline_props=Counter(e.text for e in root.iter('TimelineProp'))
    references=sorted(set(e.text for e in root.iter() if e.text and re.search(r'\.(?:png|jpg|bmp|dds|xur|wav|xma|ttf|xtt)(?:$|[,;])',e.text,re.I)))
    records.append(dict(file=source.name,sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        node_classes=dict(classes.most_common()),properties=dict(props.most_common()),
        timeline_properties=dict(timeline_props.most_common()),
        named_frames=len(list(root.iter('NamedFrame'))),
        visual_ids=[e.findtext('Properties/Id') for e in root.iter('XuiVisual')],
        resource_references=references))
groups_path=args.source_folder/'layout-groups.json'
groups=[g for g in json.loads(groups_path.read_text(encoding='utf-8')) if g['family']==0]
report=dict(policy='Read original scene/resource data first; record an explicit gap before estimating.',
    existing_build_inventory='scene-audit/update-evidence.json',
    existing_image_inventory='360dasharchive/image-inventory.json',
    existing_layout_groups=groups,decoded_scenes=records,
    runtime_scope='The native renderer imports a subset. Inventory presence does not mean a property is implemented.')
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(f'{len(records)} decoded source scenes inventoried; reused {len(groups)} existing Blades layout groups')
