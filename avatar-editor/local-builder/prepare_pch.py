"""Write portable source wrappers for a fresh native pack PCH build.

No compiler is invoked here. SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import argparse
import hashlib
import json
from source_patch import relative_file


def prepare(run):
    run=Path(run).resolve(strict=True)
    contract=json.loads(Path(__file__).with_name('pch-producer.json').read_text())
    source=relative_file(run,contract['included_source'])
    if hashlib.sha256(source.read_bytes()).hexdigest()!=contract['included_source_sha256']:
        raise ValueError('PCH input source differs')
    wrapper=relative_file(run,contract['wrapper'])
    include=(wrapper.parent/contract['relative_include']).resolve()
    if include!=source.resolve() or not include.is_relative_to(run):
        raise ValueError('PCH relative include does not resolve to its pinned source')
    outputs=[]
    for item in contract['source_outputs']:
        path=relative_file(run,item['path']);data=item['content'].encode()
        if path.exists() or hashlib.sha256(data).hexdigest()!=item['sha256']:
            raise ValueError('PCH wrapper output exists or differs')
        outputs.append((path,data))
    for path,data in outputs:
        path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('xb') as stream:stream.write(data)
    return {'status':'PASS','scope':'portable PCH source wrappers only','native_compilation':False,
            'files':[{ 'path':item['path'],'sha256':item['sha256']} for item in contract['source_outputs']]}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',required=True,type=Path)
    args=parser.parse_args()
    print(json.dumps(prepare(args.run),indent=2))
