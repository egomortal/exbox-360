"""Inventory original gradient transforms across every recovered era and revision."""
import argparse, collections, hashlib, json, math
from pathlib import Path
import xml.etree.ElementTree as ET
from scene_catalog import inventory_path,protect_active_output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inventory', type=Path)
    args = parser.parse_args()
    protect_active_output(args.assets,args.output)
    catalog = json.loads(inventory_path(args.assets,args.inventory).read_text())
    matrix = json.loads((args.assets/'ERA_COMPLETENESS.json').read_text())
    eras = {revision: era['era'] for era in matrix['eras'] for revision in era['revisions']}
    associations = collections.defaultdict(list)
    for update in catalog['updates']:
        for variant in update['variants']:
            for scene in variant['scenes']:
                associations[scene['sha256']].append(dict(revision=update['build'], era=eras[update['build']],
                    path=scene['path'], package=scene['package'], scope=update.get('source_scope','dashboard_module_resources')))
    records = {}
    for identity, info in catalog['scenes'].items():
        if not info.get('xml'):
            records[identity] = dict(status='decode_gap', uses=associations[identity])
            continue
        path = args.assets/info['xml']
        raw = path.read_bytes()
        assert hashlib.sha256(raw).hexdigest() == info['xml_sha256'], path
        root = ET.fromstring(raw)
        figures = []
        for index, figure in enumerate(root.iter('XuiFigure')):
            fill = figure.find('Properties/Fill/Properties')
            if fill is None or fill.find('Gradient/Properties') is None:
                continue
            scale = [float(v.strip()) for v in (fill.findtext('Scale') or '1,1,1').split(',')]
            rotation = float(fill.findtext('Rotation') or 0)
            figures.append(dict(ordinal=index, id=figure.findtext('Properties/Id'), scale=scale, rotation=rotation,
                nonuniform_rotated=abs(scale[0]-scale[1])>1e-6 and abs(math.sin(math.radians(rotation)))>1e-6,
                signed_or_zero=any(v<=0 for v in scale[:2])))
        animated = []
        for timeline in root.iter('Timeline'):
            props = [p.text for p in timeline.findall('TimelineProp')]
            relevant = [p for p in props if p in ('Fill.Scale','Fill.Rotation','Fill.Translation')]
            if relevant:
                animated.append(dict(id=timeline.findtext('Id'), properties=relevant, keys=len(timeline.findall('KeyFrame'))))
        records[identity] = dict(status='decoded', xml=info['xml'], xml_sha256=info['xml_sha256'],
            figures=figures, animated_transform_tracks=animated, uses=associations[identity])
    summaries = []
    for era in matrix['eras']:
        revisions = []
        for revision in era['revisions']:
            selected = [r for r in records.values() if any(u['revision']==revision for u in r['uses'])]
            decoded = [r for r in selected if r['status']=='decoded']
            revisions.append(dict(revision=revision, unique_scenes=len(selected), decoded=len(decoded),
                decode_gaps=len(selected)-len(decoded), gradients=sum(len(r['figures']) for r in decoded),
                nonuniform_rotated=sum(f['nonuniform_rotated'] for r in decoded for f in r['figures']),
                signed_or_zero=sum(f['signed_or_zero'] for r in decoded for f in r['figures']),
                animated_transform_tracks=sum(len(r['animated_transform_tracks']) for r in decoded)))
        summaries.append(dict(era=era['era'], revisions=revisions))
    report = dict(scenes=records, eras=summaries,
        limits=['Static source audit does not verify final colors, GPU sampling, inherited host bindings or animation output.',
                'Animated transform tracks are counted, not flattened or substituted.',
                'CPU brush contract independently verified only for original and transitional Blades; later eras require their own runtime verification.'])
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(unique_scenes=len(records), decoded=sum(r['status']=='decoded' for r in records.values()),
        eras=len(summaries), revisions=sum(len(e['revisions']) for e in summaries))))


if __name__ == '__main__':
    main()
