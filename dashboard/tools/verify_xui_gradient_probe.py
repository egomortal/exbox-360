"""Compare the bounded C++ early-XAM ramp with preserved all-era stop sets."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import numpy as np
from scene_catalog import active_bundle,protect_active_output,_verified


def model(positions, colors):
    stops=np.asarray(positions,dtype=np.float32)
    channels=np.asarray([[(c>>s)&255 for s in (16,8,0,24)] for c in colors],dtype=np.float32)
    channels=np.float32(channels*np.float32(1/255))
    result=[]; lower=0;upper=1;saturated=0;extrapolated=0
    maximum=np.asarray(0x404000ff,dtype=np.uint32).view(np.float32)
    for index in range(128):
        phase=np.float32(index/128)
        if phase>stops[upper]:
            lower=upper
            if upper+1<len(stops):upper+=1
        span=np.float32(stops[upper]-stops[lower])
        if upper==lower or span<np.float32(.0001):factor=np.float32(1)
        elif phase<stops[lower]:factor=np.float32(0)
        else:factor=np.float32(np.float32(phase-stops[lower])/span)
        extrapolated+=int(factor<0 or factor>1)
        difference=np.float32(channels[upper]-channels[lower])
        value=np.float32(difference.astype(np.float64)*float(factor)+channels[lower].astype(np.float64))
        encoded=np.float32(3-value.astype(np.float64)*float(np.float32(-255/(2**22))))
        saturated+=int(np.any(encoded<3)|np.any(encoded>maximum))
        encoded=np.minimum(np.fmax(encoded,np.float32(3)),maximum)
        r,g,b,a=map(int,encoded.view(np.uint32)&255)
        result.append((a<<24)|(r<<16)|(g<<8)|b)
    return np.asarray(result,dtype=np.uint32),extrapolated,saturated


def linux(path):
    path=path.resolve().as_posix()
    return '/mnt/'+path[0].lower()+path[2:]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--catalog',type=Path)
    p.add_argument('--catalog-sha256')
    a=p.parse_args();protect_active_output(a.assets,a.out);a.out.mkdir(exist_ok=True,parents=True)
    source=a.catalog or a.assets/'blades-gradient-texture-20261026/gradient-probes/gradient-probe.json'
    raw=source.read_bytes();catalog=json.loads(raw)
    catalog_hash=hashlib.sha256(raw).hexdigest()
    selection=dict(kind='historical_default',verified_sha256=catalog_hash)
    if a.catalog:
        bundle=active_bundle(a.assets)
        active_report=_verified(a.assets,bundle['reports']['gradient-probes/gradient-probe.json']) if bundle else None
        if active_report and source.resolve()==active_report.resolve():
            pointer=a.assets/'scene-audit/revisions/ACTIVE.json'
            pointer_record=json.loads(pointer.read_text())
            selection=dict(kind='active_manifest_report',release=bundle['release'],
                           manifest=pointer_record['manifest'],verified_sha256=catalog_hash,
                           pointer_sha256=hashlib.sha256(pointer.read_bytes()).hexdigest())
        elif not a.catalog_sha256:
            raise ValueError('Explicit non-active catalog requires --catalog-sha256')
        if a.catalog_sha256:
            if catalog_hash!=a.catalog_sha256.lower():raise ValueError('Explicit catalog SHA-256 mismatch')
            selection['explicit_sha256_verified']=True
    keys=[]; rejected=[]
    for key in sorted(catalog['gradients']):
        positions=np.asarray(catalog['gradients'][key]['positions'],dtype=np.float32)
        if len(positions)<2 or np.any(np.diff(positions)<0) or not np.all(np.isfinite(positions)):
            rejected.append(dict(gradient=key,reason='Short/unsorted/nonfinite source stops; upstream validation remains unverified'))
        else:keys.append(key)
    for row in rejected:
        row['source_uses']=[dict(scene=identity,uses=scene.get('uses',[]))
            for identity,scene in catalog['scenes'].items()
            if any(g['gradient']==row['gradient'] for g in scene.get('gradients',[]))]
    input_path=a.out/'source-stop-sets.txt'
    with input_path.open('w') as stream:
        for key in keys:
            gradient=catalog['gradients'][key]
            stream.write(str(len(gradient['positions']))+'\n')
            for position,color in zip(gradient['positions'],gradient['colors']):
                stream.write(format(float(np.float32(position)),'.9g')+' '+str(int(color,16))+'\n')
    exe=a.out/'verify_xui_gradient'
    command=['wsl.exe','--cd',str(a.work),'g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
             '-ffp-contract=off','tools/verify_xui_gradient.cpp','-o',linux(exe)]
    compile_run=subprocess.run(command,capture_output=True,text=True)
    (a.out/'compile.log').write_text(compile_run.stdout+compile_run.stderr)
    assert compile_run.returncode==0,compile_run.stderr
    output=a.out/'all-source-ramps.le32'
    command_run=['wsl.exe','--cd',str(a.work),linux(exe),linux(input_path),linux(output)]
    test_run=subprocess.run(command_run,capture_output=True,text=True)
    (a.out/'test.log').write_text(test_run.stdout+test_run.stderr)
    assert test_run.returncode==0,test_run.stdout+test_run.stderr
    bytes_=output.read_bytes();assert len(bytes_)==len(keys)*128*4
    cpp=np.frombuffer(bytes_,dtype='<u4').reshape((len(keys),128))
    rows=[];old_equal=0;newly_verified=0;extrapolated=0;saturated=0
    for index,key in enumerate(keys):
        gradient=catalog['gradients'][key]
        reference,extra,sat=model(gradient['positions'],[int(c,16) for c in gradient['colors']])
        np.testing.assert_array_equal(cpp[index],reference)
        old=gradient.get('file')
        if old:
            original_bytes=(source.parent/old).read_bytes()
            assert hashlib.sha256(original_bytes).hexdigest()==gradient['sha256']
            original=np.frombuffer(original_bytes,dtype='>u4')
            np.testing.assert_array_equal(cpp[index],original)
            old_equal+=1
        else:newly_verified+=1
        extrapolated+=extra;saturated+=sat
        rows.append(dict(gradient=key,cpp_offset_bytes=index*512,texels=128,
                         old_probe_unchanged=bool(old),extrapolated_texels=extra,
                         saturated_texels=sat,sha256=hashlib.sha256(cpp[index].astype('>u4').tobytes()).hexdigest()))
    era_summary=[]
    valid_keys=set(keys)
    for era in catalog['eras']:
        selected=[s for s in catalog['scenes'].values() if any(u['era']==era['era'] for u in s['uses'])]
        sets={g['gradient'] for scene in selected for g in scene.get('gradients',[])}
        era_summary.append(dict(era=era['era'],revisions=era['revisions'],gradient_sets=len(sets),
            probed=len(sets & valid_keys),rejected=len(sets-valid_keys),
            runtime_scope='early XAM only; later-era inputs retain unverified runtime equivalence'))
    report=dict(scope='early XAM CPU/packing contract; later-era stop sets are diagnostic inputs only',
        source_catalog=str(source),source_sha256=catalog_hash,catalog_selection=selection,
        discovered_stop_sets=len(catalog['gradients']),stop_sets=len(keys),texels=len(keys)*128,cpp_scalar_model_matches=True,
        prior_probes_byte_identical=old_equal,previously_rejected_probes_completed=newly_verified,
        extrapolated_texels=extrapolated,saturated_texels=saturated,rows=rows,rejected=rejected,
        compile_command=command,test_command=command_run,test_stdout=test_run.stdout,
        source_era_coverage=era_summary,output_sha256=hashlib.sha256(bytes_).hexdigest(),
        provenance='derived CPU research probes; not original bitmap bytes or active renderer output',
        limitations=[('Selected report belongs to the hash-verified active float32 release.' if selection['kind']=='active_manifest_report'
                      else 'Retained source catalog contains historical six-decimal positions; active float32 catalog is separate.'),
                     'Packing semantics corroborated by pinned Xenia implementation, not an Xbox hardware capture.',
                     'Scalar flow/VMX operations are transcribed, not the full original function executed.',
                     'Exact GPU subtexel interpolation precision, gamma and final pixel rounding are unverified.',
                     'No changes to active rendering, packs, images or PS5.'])
    (a.out/'RAMP_VERIFICATION.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k in ('stop_sets','texels','prior_probes_byte_identical','previously_rejected_probes_completed','extrapolated_texels','saturated_texels')}))


if __name__=='__main__':main()
