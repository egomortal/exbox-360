"""Decode a versioned candidate catalog; verify numeric-only XML changes and float32 audits."""
from pathlib import Path
import argparse, collections, concurrent.futures, hashlib, json, math, os, struct, subprocess, time
import xml.etree.ElementTree as ET
from scene_catalog import inventory_path,protect_active_output

def sha(raw):return hashlib.sha256(raw).hexdigest()
def bits(number):return struct.pack('>f',float(number)).hex()

def compare(old,new):
    changes=[]
    def visit(a,b,path):
        assert a.tag==b.tag and a.attrib==b.attrib and len(a)==len(b),(path,'structure changed')
        if not len(a) and (a.text or '')!=(b.text or ''):
            left=(a.text or '').split(',');right=(b.text or '').split(',')
            assert len(left)==len(right),(path,'component count')
            changed=[]
            for ordinal,(x,y) in enumerate(zip(left,right)):
                if x==y:continue
                oldnum=float(x);newnum=float(y)
                assert math.isfinite(oldnum) and math.isfinite(newnum),(path,'nonfinite')
                # Only differences explained by the old six-decimal writer are allowed.
                original_float32=struct.unpack('>f',struct.pack('>f',newnum))[0]
                assert bits(x)==bits(y) or abs(oldnum-original_float32)<=0.00000051,(path,x,y)
                changed.append(dict(component=ordinal,old=x,new=y,old_bits=bits(x),new_bits=bits(y)))
            changes.append(dict(path=path,values=changed))
        for i,(ac,bc) in enumerate(zip(a,b)):visit(ac,bc,path+'/'+ac.tag+'['+str(i)+']')
    visit(old,new,'/'+old.tag)
    return changes

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--inventory',type=Path)
    p.add_argument('--dotnet',type=Path,required=True);p.add_argument('--cli',type=Path,required=True)
    a=p.parse_args();a.assets=a.assets.resolve();a.output=a.output.resolve();protect_active_output(a.assets,a.output);a.output.mkdir(parents=True,exist_ok=True)
    catalog_path=inventory_path(a.assets,a.inventory)
    inventory=json.loads(catalog_path.read_text())
    matrix=json.loads((a.assets/'ERA_COMPLETENESS.json').read_text());eras={r:e['era'] for e in matrix['eras'] for r in e['revisions']}
    sources={};uses=collections.defaultdict(list)
    for update in inventory['updates']:
        for variant in update['variants']:
            for scene in variant['scenes']:
                sources.setdefault(scene['sha256'],a.assets/'360dasharchive'/scene['path'])
                uses[scene['sha256']].append(dict(revision=update['build'],era=eras[update['build']],path=scene['path']))
    env=dict(os.environ,DOTNET_ROOT=str(a.dotnet.parent),DOTNET_ROLL_FORWARD='Major')
    def decode(entry):
        identity,record=entry
        source=sources[identity];raw=source.read_bytes();assert sha(raw)==identity
        version=struct.unpack_from('>I',raw,4)[0]
        base=dict(source=str(source),source_sha256=identity,version=version,uses=uses[identity])
        if not record.get('xml'):return identity,dict(base,status='retained_decode_gap',error=record.get('error','unknown'))
        old=a.assets/record['xml'];assert sha(old.read_bytes())==record['xml_sha256']
        candidate=a.output/'decoded'/(identity+'.xui');candidate.parent.mkdir(exist_ok=True)
        proof=candidate.with_suffix('.numeric.json');log=candidate.with_suffix('.log')
        if version==7:
            candidate.write_bytes(old.read_bytes())
            return identity,dict(base,status='retained_v7_nine_digit',xml=str(candidate.relative_to(a.assets)),xml_sha256=sha(candidate.read_bytes()),old_xml=record['xml'],old_xml_sha256=record['xml_sha256'],changes=[])
        assert version in (5,8),version
        started=time.perf_counter()
        result=subprocess.run([str(a.dotnet),str(a.cli),'conv','-s',str(source),'-f','xuiv12','-o',str(candidate),'-g','V'+str(version),'-i','-l',str(log),'-v','info'],capture_output=True,text=True,env=dict(env,XUI_NUMERIC_AUDIT=str(proof)),timeout=60)
        if result.returncode or not candidate.exists():return identity,dict(base,status='candidate_decode_failure',error=result.stdout+result.stderr)
        try:changes=compare(ET.parse(old).getroot(),ET.parse(candidate).getroot())
        except (AssertionError,ValueError) as exc:
            return identity,dict(base,status='candidate_comparison_failure',error=str(exc))
        audit=json.loads(proof.read_text()) if proof.exists() else dict(calls=0,values=[],legacy_bit_changes=0,nonfinite=0)
        assert audit['nonfinite']==0
        for value in audit['values']:
            assert bits(value['text'])==value['bits'],value
        # Presence is only a separate byte cross-check, not proof of field/schema binding.
        absent=[v['bits'] for v in audit['values'] if bytes.fromhex(v['bits']) not in raw]
        changed_bits=sum(v['old_bits']!=v['new_bits'] for c in changes for v in c['values'])
        return identity,dict(base,status='candidate_verified',xml=str(candidate.relative_to(a.assets)),xml_sha256=sha(candidate.read_bytes()),old_xml=record['xml'],old_xml_sha256=record['xml_sha256'],changes=changes,changed_numeric_components=changed_bits,
            audit=dict(calls=audit['calls'],legacy_bit_changes=audit['legacy_bit_changes'],unique_values=len(audit['values']),all_serialized_float32_bits_preserved=True,values_not_present_in_raw_source=absent,byte_presence_scope='Weak cross-check only; defaults and computed values can be absent.'),decode_wall_seconds=time.perf_counter()-started)
    records={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        for i,(identity,result) in enumerate(executor.map(decode,inventory['scenes'].items()),1):
            records[identity]=result
            if i%50==0:print(json.dumps(dict(scenes=i,total=len(inventory['scenes']))),flush=True)
    summaries=[]
    for era in matrix['eras']:
        chosen=[r for r in records.values() if any(u['era']==era['era'] for u in r['uses'])]
        summaries.append(dict(era=era['era'],revisions=len(era['revisions']),scenes=len(chosen),statuses=dict(collections.Counter(r['status'] for r in chosen)),changed_scenes=sum(bool(r.get('changes')) for r in chosen),changed_numeric_components=sum(r.get('changed_numeric_components',0) for r in chosen)))
    report=dict(decoder_cli=str(a.cli),decoder_sha256=sha(a.cli.read_bytes()),source_inventory_sha256=sha(catalog_path.read_bytes()),scenes=records,eras=summaries,
        status='isolated_candidate_catalog; current runtime/cache not replaced',
        limitations=['Writer round-trip verifies loaded float32 values, not every decoder schema or custom-curve meaning.',
            'V7 scenes retain the existing strict decoder and nine-significant-digit output; audited separately.',
            'Raw byte presence is not a property-to-offset mapping; original CUST bounds stay separately preserved in XUR.',
            'No rendered comparison, pack promotion, performance claim or console action follows automatically.'])
    report['revisions']=[]
    for era in matrix['eras']:
        for revision in era['revisions']:
            chosen=[r for r in records.values() if any(u['revision']==revision for u in r['uses'])]
            update=next(u for u in inventory['updates'] if u['build']==revision)
            report['revisions'].append(dict(era=era['era'],revision=revision,source_scope=update.get('source_scope','recovered_dashboard_modules'),scenes=len(chosen),
                statuses=dict(collections.Counter(r['status'] for r in chosen)),changed_numeric_components=sum(r.get('changed_numeric_components',0) for r in chosen)))
    (a.output/'numeric-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    candidate=json.loads(json.dumps(inventory))
    for identity,record in candidate['scenes'].items():
        migrated=records[identity]
        if migrated.get('xml'):record.update(xml=migrated['xml'],xml_sha256=migrated['xml_sha256'])
    candidate['numeric_serialization']=dict(mode='isolated_invariant_roundtrip_candidate',audit=str((a.output/'numeric-audit.json').relative_to(a.assets)),original_inventory=str(catalog_path))
    (a.output/'inventory.json').write_text(json.dumps(candidate,indent=2)+'\n')
    print(json.dumps(dict(statuses=dict(collections.Counter(r['status'] for r in records.values())),eras=summaries)),flush=True)

if __name__=='__main__':main()
