"""Audit source metrics for all eras without replacing runtime audit evidence."""
from pathlib import Path
import argparse, collections, hashlib, json, math, re
import xml.etree.ElementTree as ET
from scene_catalog import catalog_pair,protect_active_output

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--inventory',type=Path);p.add_argument('--timing',type=Path)
a=p.parse_args();protect_active_output(a.assets,a.output);a.output.mkdir(parents=True,exist_ok=True)
inventory_file,timing_file=catalog_pair(a.assets,a.inventory,a.timing)
inventory=json.loads(inventory_file.read_text())
timing=json.loads(timing_file.read_text())
matrix=json.loads((a.assets/'ERA_COMPLETENESS.json').read_text())
reports=[]
for era in matrix['eras']:
    updates=[u for u in inventory['updates'] if u['build'] in era['revisions']]
    identities=sorted({s['sha256'] for u in updates for v in u['variants'] for s in v['scenes']})
    classes=collections.Counter();properties=collections.Counter();fonts=collections.Counter();points=collections.Counter();colors=collections.Counter();flags=collections.Counter()
    sizes=collections.Counter();errors=[];gaps=[];refs=[]
    for identity in identities:
        info=inventory['scenes'][identity]
        if 'error' in info:gaps.append(dict(sha256=identity,error=info['error']));continue
        path=a.assets/info['xml'];data=path.read_bytes();assert hashlib.sha256(data).hexdigest()==info['xml_sha256']
        root=ET.fromstring(data)
        for child in root:sizes[(child.tag,child.findtext('Properties/Width'),child.findtext('Properties/Height'))]+=1
        for node in root.iter():
            if node.tag.startswith('Xui') or node.find('Properties') is not None:classes[node.tag]+=1
            if node.tag=='OriginalFlag':flags[node.text]+=1
            if node.tag!='Properties':continue
            for prop in node:
                properties[prop.tag]+=1;value=prop.text or ''
                if prop.tag in ('Typeface','Font','FontName'):fonts[value]+=1
                if prop.tag=='PointSize':points[value]+=1
                if 'color' in prop.tag.lower() or 'colour' in prop.tag.lower():colors[value]+=1
                if prop.tag in ('Width','Height','PointSize') and value:
                    try:
                        number=float(value)
                        if not math.isfinite(number) or number<0:errors.append(dict(scene=identity,property=prop.tag,value=value))
                    except ValueError:errors.append(dict(scene=identity,property=prop.tag,value=value))
                if value and (prop.tag=='Visual' or re.search(r'\.(png|jpg|xpr|xzp|xma|wav|wma|xur|ttf)$',value,re.I)):
                    refs.append(dict(scene=identity,property=prop.tag,value=value))
    revisions=[]
    for update in updates:
        sources={s['sha256'] for v in update['variants'] for s in v['scenes']}
        t=next(t for t in timing['updates'] if t['build']==update['build'] and t['family']==update['family'])
        revisions.append(dict(build=update['build'],source_scope=update.get('source_scope','dashboard_module_resources'),source_scenes=len(sources),decoded=sum('error' not in inventory['scenes'][s] for s in sources),timed_scenes=len(t['timed_scenes']),decode_gaps=t['unresolved']))
    report=dict(era=era['era'],revisions=revisions,unique_scenes=len(identities),decoded_scenes=len(identities)-len(gaps),classes=dict(classes),properties=dict(properties),fonts=dict(fonts),point_sizes=dict(points),source_color_literals=dict(colors),root_dimensions=[dict(kind=k[0],width=k[1],height=k[2],scenes=v) for k,v in sizes.items()],retained_original_flags=dict(flags),dimension_findings=errors,source_references=refs,decode_gaps=gaps,
        limitations=['Static source audit; existing native visual/navigation/performance/reference evidence remains separate.',
            'A source reference is a candidate mapping, not proof of live host lookup or rendering.',
            'Unknown/unreferenced resources are retained; decoder presence is not fidelity completion.'])
    (a.output/(era['era']+'.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    reports.append({k:report[k] for k in ('era','revisions','unique_scenes','decoded_scenes')})
(a.output/'summary.json').write_text(json.dumps(dict(eras=reports),indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(eras=len(reports),revisions=sum(len(r['revisions']) for r in reports))))
