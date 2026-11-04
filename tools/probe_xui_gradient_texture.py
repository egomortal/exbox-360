"""Probe the original 128-texel CPU ramp algorithm without changing live assets.

Source XML positions are decoded derivatives and can have decimal rounding.
Sampler interpolation here is a diagnostic model, not Xbox GPU precision parity.
"""
import argparse, collections, hashlib, json, struct
from pathlib import Path
import numpy as np
import xml.etree.ElementTree as ET
from scene_catalog import inventory_path,protect_active_output


def original_ramp(positions,colors,width=128):
    positions=np.asarray(positions,dtype=np.float32)
    if len(positions)<2 or len(positions)!=len(colors) or np.any(np.diff(positions)<0):
        raise ValueError('Original gradient requires two sorted stops')
    channels=np.array([[(color>>shift)&255 for shift in (16,8,0,24)] for color in colors],dtype=np.float32)
    channels=np.float32(channels*np.float32(1/255))
    lower,upper=0,1;result=[]
    for index in range(width):
        phase=np.float32(np.float32(index)/np.float32(width))
        if phase>positions[upper]:
            lower=upper
            if upper+1<len(positions):upper+=1
        span=np.float32(positions[upper]-positions[lower])
        if upper==lower or span<np.float32(.0001):factor=np.float32(1)
        elif phase<positions[lower]:factor=np.float32(0)
        else:factor=np.float32(np.float32(phase-positions[lower])/span)
        if not 0<=factor<=1:
            raise ValueError('Dense-stop overshoot requires separately verified pack saturation semantics')
        difference=np.float32(channels[upper]-channels[lower])
        value=np.float32(difference.astype(np.float64)*float(factor)+channels[lower].astype(np.float64))
        # The original VMX sequence creates 3 + channel*255/2^22, then packs its
        # low component bytes. Float32 conversion supplies ties-to-even rounding.
        packed=np.float32(3-value.astype(np.float64)*float(np.float32(-255/(2**22))))
        bytes_=packed.view(np.uint32)&255
        red,green,blue,alpha=map(int,bytes_)
        result.append((alpha<<24)|(red<<16)|(green<<8)|blue)
    return np.array(result,dtype=np.uint32)


def diagnostic_linear_sample(ramp,phase):
    position=np.clip(np.asarray(phase)*len(ramp)-.5,0,len(ramp)-1)
    left=np.floor(position).astype(np.int32);right=np.minimum(left+1,len(ramp)-1)
    blend=position-left
    channels=np.array([[(int(c)>>shift)&255 for shift in (0,8,16,24)] for c in ramp])
    return channels[left]*(1-blend[...,None])+channels[right]*blend[...,None]


def self_check():
    ramp=original_ramp([0,1],[0xff000000,0xffffffff])
    assert int(ramp[0])==0xff000000 and int(ramp[64])==0xff808080
    assert int(ramp[-1])==0xfffdfdfd
    ramp=original_ramp([0,1],[0x00000000,0xffffffff])
    assert int(ramp[64])==0x80808080
    ramp=original_ramp([0,.5,.5,1],[0xff000000,0xffff0000,0xff00ff00,0xffffffff])
    assert int(ramp[64])==0xffff0000 and int(ramp[65])>>16&255<16
    sample=diagnostic_linear_sample(np.array([0xff000000,0xffffffff],dtype=np.uint32),np.array([0,.5,1]))
    np.testing.assert_array_equal(sample[:,0],[0,127.5,255])
    try:original_ramp([0,.001,.002,1],[0,0xffffffff,0xff000000,0xffffffff])
    except ValueError:pass
    else:raise AssertionError('Unverified dense-stop overshoot was not rejected')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--inventory',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();protect_active_output(a.assets,a.output);a.output.mkdir(parents=True,exist_ok=True)
    self_check()
    inventory=json.loads(inventory_path(a.assets,a.inventory).read_text())
    matrix=json.loads((a.assets/'ERA_COMPLETENESS.json').read_text())
    eras={revision:era['era'] for era in matrix['eras'] for revision in era['revisions']}
    uses=collections.defaultdict(list)
    for update in inventory['updates']:
        for variant in update['variants']:
            for scene in variant['scenes']:
                uses[scene['sha256']].append(dict(revision=update['build'],era=eras[update['build']],path=scene['path']))
    gradients={};scenes={}
    for identity,scene in inventory['scenes'].items():
        if not scene.get('xml'):
            scenes[identity]=dict(status='decode_gap',uses=uses[identity]);continue
        path=a.assets/scene['xml'];raw=path.read_bytes()
        assert hashlib.sha256(raw).hexdigest()==scene['xml_sha256']
        references=[]
        for ordinal,gradient in enumerate(ET.fromstring(raw).findall('.//Fill/Properties/Gradient/Properties')):
            positions=[float(p.text) for p in gradient.findall('StopPos')]
            colors=[int(p.text,16) for p in gradient.findall('StopColor')]
            key=hashlib.sha256(b''.join(struct.pack('>fI',p,c) for p,c in zip(positions,colors))).hexdigest()
            if key not in gradients:
                record=dict(positions=positions,colors=[hex(c) for c in colors],source='decoded original XML; decimal precision may differ from original XUR float32')
                try:
                    ramp=original_ramp(positions,colors);file=a.output/(key+'.ramp128')
                    file.write_bytes(ramp.astype('>u4').tobytes())
                    record.update(status='cpu_algorithm_probe',file=file.name,sha256=hashlib.sha256(file.read_bytes()).hexdigest(),texels=128)
                except ValueError as exc:record.update(status='unverified',reason=str(exc))
                gradients[key]=record
            references.append(dict(ordinal=ordinal,gradient=key))
        scenes[identity]=dict(status='decoded',xml=scene['xml'],xml_sha256=scene['xml_sha256'],gradients=references,uses=uses[identity])
    summaries=[]
    for era in matrix['eras']:
        selected=[s for s in scenes.values() if any(u['era']==era['era'] for u in s['uses'])]
        unique={g['gradient'] for s in selected for g in s.get('gradients',[])}
        summaries.append(dict(era=era['era'],revisions=len(era['revisions']),scenes=len(selected),
            decoded=sum(s['status']=='decoded' for s in selected),gradient_sets=len(unique),
            probed=sum(gradients[k]['status']=='cpu_algorithm_probe' for k in unique),
            runtime_contract='1888 probe; later-era CPU/sampler semantics unverified'))
    report=dict(gradients=gradients,scenes=scenes,eras=summaries,
        width=128,phase='texel_index / width; last texel is not phase 1',
        provenance='Derived research probe, not original shader bytes or replacement dashboard artwork.',
        limitations=['Original PPC scalar/VMX logic is transcribed, not executed end-to-end or captured from hardware.',
            'Dense-stop overshoot and malformed/unsorted stops are preserved as unverified rather than silently approximated.',
            'XML decimal rounding, GPU filtering precision, sampler translation, gamma and host themes remain unverified.',
            'Animated stop tracks require actual runtime values; static stop presence does not verify animation output.',
            'No renderer or live dashboard asset uses these probe files.'])
    (a.output/'gradient-probe.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(unique_stop_sets=len(gradients),probed=sum(r['status']=='cpu_algorithm_probe' for r in gradients.values()),eras=summaries)))


if __name__=='__main__':main()
