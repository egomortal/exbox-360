"""Trace authored visual-root properties across recovered scenes and compiled packs."""
import argparse, collections, hashlib, json, struct
from pathlib import Path
import xml.etree.ElementTree as ET
from compile_xui_scene import FIELDS
from scene_catalog import inventory_path,protect_active_output


def pack_nodes(path):
    raw=path.read_bytes();counts=struct.unpack_from('<8I',raw,8)
    node_size=64+20*len(FIELDS)
    end=40+counts[0]*node_size
    strings_offset=end+counts[1]*28+counts[2]*24+counts[3]*40+counts[4]*16+counts[5]*8+counts[6]*8
    strings=raw[strings_offset:]
    nodes=[]
    for index in range(counts[0]):
        offset=40+index*node_size;meta=struct.unpack_from('<12i4f',raw,offset)
        identity=strings[meta[4]:].split(b'\0',1)[0].decode('utf-8')
        nodes.append((identity,raw[offset:offset+64],
            [raw[offset+64+field*20:offset+84+field*20] for field in range(len(FIELDS))]))
    return raw,counts,end,nodes


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--before',type=Path)
    p.add_argument('--after',type=Path)
    p.add_argument('--source-only',action='store_true')
    p.add_argument('--inventory',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    protect_active_output(a.assets,a.output)
    if not a.source_only and (not a.before or not a.after):p.error('Provide --before and --after, or use --source-only.')
    if a.source_only and (a.before or a.after):p.error('Source-only mode does not compare packs.')
    inventory=json.loads(inventory_path(a.assets,a.inventory).read_text())
    matrix=json.loads((a.assets/'ERA_COMPLETENESS.json').read_text())
    eras={revision:era['era'] for era in matrix['eras'] for revision in era['revisions']}
    uses=collections.defaultdict(list)
    for update in inventory['updates']:
        for variant in update['variants']:
            for scene in variant['scenes']:
                uses[scene['sha256']].append(dict(era=eras[update['build']],revision=update['build'],path=scene['path']))
    scenes={}
    for identity,info in inventory['scenes'].items():
        if not info.get('xml'):
            scenes[identity]=dict(status='decode_gap',uses=uses[identity]);continue
        raw=(a.assets/info['xml']).read_bytes()
        assert hashlib.sha256(raw).hexdigest()==info['xml_sha256']
        root=ET.fromstring(raw);visuals=[];references=set()
        for prop in root.findall('.//Visual'):
            if prop.text:references.add(prop.text)
        for timeline in root.iter('Timeline'):
            fields=[prop.text for prop in timeline.findall('TimelineProp')]
            for index,field in enumerate(fields):
                if field!='Visual':continue
                for key in timeline.findall('KeyFrame'):
                    values=key.findall('Prop')
                    if index<len(values) and values[index].text:references.add(values[index].text)
        for visual in root.findall('XuiVisual'):
            prop=visual.find('Properties')
            values={child.tag:child.text for child in prop if len(child)==0}
            preserved={key:value for key,value in values.items() if key not in ('Id','Width','Height')}
            visuals.append(dict(id=values.get('Id'),authored_width=values.get('Width'),
                authored_height=values.get('Height'),root_properties=preserved,
                child_count=sum(child.tag.startswith('Xui') for child in visual)))
        scenes[identity]=dict(status='decoded',xml=info['xml'],xml_sha256=info['xml_sha256'],visuals=visuals,
            explicit_visual_references=sorted(references),possible_implicit_class_visuals=sorted({node.tag for node in root.iter() if node.tag.startswith('Xui')}),uses=uses[identity])
    packs=[]
    for after in sorted(a.after.glob('*.nxui')) if not a.source_only else []:
        before=a.before/after.name
        old,old_counts,old_end,old_nodes=pack_nodes(before)
        new,new_counts,new_end,new_nodes=pack_nodes(after)
        assert old_counts==new_counts,after
        assert old[old_end:]==new[new_end:],f'Original animation/path/stop/string payload changed: {after}'
        source_visuals={}
        for source in json.loads(after.with_suffix('.json').read_text())['source']:
            for visual in ET.parse(source['xml']).getroot().findall('XuiVisual'):
                source_visuals[visual.findtext('Properties/Id')]=visual
        changes=[]
        for index,(left,right) in enumerate(zip(old_nodes,new_nodes)):
            assert left[:2]==right[:2],f'Node identity/metadata changed: {after}, {index}'
            fields=[FIELDS[n] for n,(x,y) in enumerate(zip(left[2],right[2])) if x!=y]
            if not fields:continue
            assert left[0].startswith('@visual:'),f'Non-visual node changed: {after}, {index}'
            assert not set(fields)&{'Width','Height'},f'Host bounds changed: {after}, {index}'
            original=source_visuals[left[0][len('@visual:'):]]
            for field in fields:
                text=original.findtext('Properties/'+'/Properties/'.join(field.split('.')))
                assert text is not None,f'Changed field absent from original: {after}, {index}, {field}'
                values=[float(value) for value in text.split(',')]
                token=int(values[0]) if field=='Anchor' else 0
                expected=struct.pack('<4fI',*(values+[0.,0.,0.,0.])[:4],token)
                assert right[2][FIELDS.index(field)]==expected,f'Root differs from original: {after}, {index}, {field}'
            changes.append(dict(node=index,id=left[0],fields=fields))
        packs.append(dict(revision=int(after.stem),before_sha256=hashlib.sha256(old).hexdigest(),
            after_sha256=hashlib.sha256(new).hexdigest(),counts=list(new_counts),changed_roots=changes,
            original_animation_path_stop_string_payload_unchanged=True))
    summary=[]
    for era in matrix['eras']:
        selected=[s for s in scenes.values() if any(u['era']==era['era'] for u in s['uses'])]
        decoded=[s for s in selected if s['status']=='decoded']
        summary.append(dict(era=era['era'],revisions=len(era['revisions']),unique_scenes=len(selected),decoded=len(decoded),
            visual_definitions=sum(len(s['visuals']) for s in decoded),
            roots_with_properties_beyond_id_and_bounds=sum(bool(v['root_properties']) for s in decoded for v in s['visuals']),
            status='source audit only; original runtime/root semantics still require per-era verification' if era['priority']>2 else 'original early resize verified and matched in 6717; final fidelity remains partial'))
    revision_bindings=[]
    for update in inventory['updates']:
        identities={scene['sha256'] for variant in update['variants'] for scene in variant['scenes']}
        selected={identity:scenes[identity] for identity in identities if scenes[identity]['status']=='decoded'}
        references={name for scene in selected.values() for name in scene['explicit_visual_references']}
        implicit={name for scene in selected.values() for name in scene['possible_implicit_class_visuals']}
        definitions=[]
        for identity,scene in selected.items():
            for ordinal,visual in enumerate(scene['visuals']):
                name=visual['id']
                status='candidate_explicit_source_reference' if name in references else 'candidate_implicit_class_visual' if name in implicit else 'unreferenced_in_decoded_sources_possible_host_lookup'
                definitions.append(dict(scene=identity,ordinal=ordinal,id=name,mapping_status=status))
        names={visual['id'] for visual in definitions}
        revision_bindings.append(dict(revision=update['build'],era=eras[update['build']],definitions=definitions,
            unmatched_explicit_references=sorted(references-names),
            undecoded_scene_count=sum(scenes[identity]['status']=='decode_gap' for identity in identities),
            limits='Matches are candidates across recovered package variants; load precedence, shared resource lookup and host-generated names remain unverified. Unreferenced definitions are retained.'))
    report=dict(scenes=scenes,eras=summary,packs=packs,revision_bindings=revision_bindings,
        contract='Preserve visual-root properties; host supplies root bounds, children retain authored design bounds.',
        limits=['Root property presence does not prove host callbacks, final colors or runtime fidelity.',
            'Unknown associations and decoder gaps are preserved; definitions can be unused or revision-specific.',
            'No packs compiled in source-only audit.' if a.source_only else 'Only Blades packs are compiled in this pass; later-era runtime use remains unverified.'])
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(eras=summary,packs=len(packs),changed_roots=sum(len(p['changed_roots']) for p in packs))))


if __name__=='__main__':main()
