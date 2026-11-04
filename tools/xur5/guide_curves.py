"""Explicit legacy Guide/notification curve compatibility; never an exact exporter.

T17: XUIB 8 HUD skins/backgrounds (13604/15574/17559) use unresolved
curve flags. Preserve their metadata in private XML, and validate only the
timelines selected by the compiler, including nested/animated visuals.
The general toxml and T15 custom-scene exporters remain strict.
"""
import copy
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from toxml import convert, node
from xur8 import decode, for_xml

# Observed in used HUD dependencies, NOT a claim about retail curve semantics.
# Match D8's fallback: low kind 0/1/2, otherwise linear; preserve times/values.
# New kinds/high-bit combinations must fail, not silently extend this policy.
LEGACY_FLAGS = {10: 0, 64: 0, 66: 2, 74: 0, 131: 0, 138: 0}
POLICY = 'guide-legacy-curves-v1'


def write_xml(root, path):
    # ElementTree.write(filename) uses text mode: Windows doubles source CRLFs.
    Path(path).write_bytes(ET.tostring(root, encoding='utf-8'))


def convert_guide(src):
    data = Path(src).read_bytes()
    if data[:8] != b'XUIB\0\0\0\x08':
        return convert(src)
    tree, decoder = decode(data)
    tree = for_xml(tree)
    clean = copy.deepcopy(tree)

    def prepare(n):
        for track in n.get('timelines', {}).get('tracks', []):
            track.pop('flags', None)
            # Intermediate XML only. Every selected key is checked below.
            for key in track['keys']:
                key[1] = key[1] if key[1] in (0, 1, 2) else 0
        for child in n.get('children', []):
            prepare(child)
    prepare(clean)
    root = node(clean, decoder)

    def annotate(n, xml, path):
        path = path+'/'+n.get('props', {}).get('Id', n['class'])
        for track, timeline in zip(n.get('timelines', {}).get('tracks', []),
                                   xml.findall('Timelines/Timeline'), strict=True):
            timeline.set('GuideSource', str(src))
            timeline.set('GuideOwner', path)
            flags = track.get('flags', [[k[1], None] for k in track['keys']])
            if len(flags) != len(track['keys']):
                raise ValueError('Guide key/flag count mismatch: '+path)
            for (flag, curve), key in zip(flags, timeline.findall('KeyFrame'), strict=True):
                ET.SubElement(key, 'OriginalFlag').text = str(flag)
                if curve is not None:
                    ET.SubElement(key, 'OriginalCurveParameters').text = json.dumps(curve)
        for child, element in zip(n.get('children', []),
                                  [c for c in xml if c.tag.startswith('Xui')], strict=True):
            annotate(child, element, path)
    annotate(tree, root, '')
    return root, decoder


def prepare_pending(compiler):
    """Validate selected tracks before writing a pack, even ignored fields.

    Keep source XML intact. Only compiler-local copies lose original metadata;
    every approximation is retained in the pack's .curves.json sidecar.
    """
    compiler.curve_approximations = []
    pending = []
    for owner, original in compiler.pending:
        timelines = copy.deepcopy(original)
        for timeline in timelines.findall('Timeline'):
            for key in timeline.findall('KeyFrame'):
                original_flag = key.findtext('OriginalFlag')
                flag = int(original_flag) if original_flag is not None else int(key.findtext('Interpolation') or 0)
                curve = key.findtext('OriginalCurveParameters')
                ease = [float(key.findtext(n) or default)
                        for n, default in (('EaseIn', '0'), ('EaseOut', '0'), ('EaseScale', '50'))]
                context = '%s %s -> %s at %s' % (timeline.get('GuideSource', ''),
                    timeline.get('GuideOwner', compiler.nodes[owner]['id']),
                    timeline.findtext('Id'), key.findtext('Time'))
                if flag not in (0, 1, 2) and flag not in LEGACY_FLAGS:
                    raise ValueError('Unsupported Guide key flag %d: %s' % (flag, context))
                if key.find('OriginalEaseCoefficients') is not None:
                    raise ValueError('Unsupported Guide original ease coefficients: '+context)
                if curve is not None and flag not in (10, 74, 138):
                    raise ValueError('Unsupported Guide curve parameters: '+context)
                mode = flag if flag in (0, 1, 2) else LEGACY_FLAGS[flag]
                if int(key.findtext('Interpolation') or 0) != mode:
                    raise ValueError('Guide decoder/compatibility mode mismatch: '+context)
                if flag not in (0, 1, 2) or ease[2] != 50:
                    compiler.curve_approximations.append(dict(
                        source=timeline.get('GuideSource'), source_owner=timeline.get('GuideOwner'),
                        owner=owner, target=timeline.findtext('Id'),
                        properties=[p.text for p in timeline.findall('TimelineProp')],
                        time=float(key.findtext('Time')), original_flag=flag,
                        original_curve=json.loads(curve) if curve is not None else None,
                        original_ease=ease, interpolation=mode,
                        ease=ease if ease[2] == 50 else [0., 0., 50.]))
                if ease[2] != 50:
                    for name, value in (('EaseIn', '0'), ('EaseOut', '0'), ('EaseScale', '50')):
                        key.find(name).text = value
                for name in ('OriginalFlag', 'OriginalCurveParameters'):
                    entry = key.find(name)
                    if entry is not None:
                        key.remove(entry)
        pending.append((owner, timelines))
    compiler.pending = pending


def write_audit(compiler, out):
    rows = compiler.curve_approximations
    report = dict(policy=POLICY, exact_curves=not rows,
                  pack_sha256=hashlib.sha256(Path(out).read_bytes()).hexdigest(),
                  source=compiler.source,
                  limitation='Listed curves use legacy fallback interpolation; retail semantics remain unresolved.',
                  approximations=rows)
    Path(out).with_suffix('.curves.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('%s: %d approximate keys; raw flags %s' %
          (POLICY, len(rows), sorted({r['original_flag'] for r in rows})))
