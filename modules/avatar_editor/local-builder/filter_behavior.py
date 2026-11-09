"""Run local Sort/Filter model and original-legend behavior checks.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from source_patch import relative_file
APP = Path(__file__).resolve().parent
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path): return json.loads(path.read_text())
def verify_inputs(run):
    contract=read(APP/'filter-contract.json')
    for relative, expected in contract['sources'].items():
        if digest(relative_file(run, relative)) != expected:
            raise ValueError('Sort/Filter source differs: '+relative)
    for relative, expected in contract['tests'].items():
        if digest(relative_file(APP, relative)) != expected:
            raise ValueError('Sort/Filter harness differs: '+relative)
    return contract

def guard(run):
    contract=verify_inputs(run)
    result=read(run/'filter-behavior/result.json')
    if (result.get('status')!='PASS' or result.get('host_execution') is not True
        or result.get('contract_sha256')!=digest(APP/'filter-contract.json')
        or result.get('sources')!=contract['sources']
        or result.get('tests')!=contract['tests']
        or {j['name'] for j in result.get('jobs',[])}!={j['name'] for j in contract['jobs']}):
        raise ValueError('Sort/Filter behavior gate has not passed for these sources')
    return result

def execute(run,compiler):
    run=run.resolve(strict=True); contract=verify_inputs(run)
    output=run/'filter-behavior'; output.mkdir(exist_ok=False)
    result=dict(status='RUNNING',host_execution=False,full_application_execution=False,
        console_contact=False,sources=contract['sources'],tests=contract['tests'],
        contract_sha256=digest(APP/'filter-contract.json'),jobs=[])
    try:
        for job in contract['jobs']:
            binary=output/job['name']
            command=[str(compiler),'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                '-ffp-model=strict','-fno-strict-aliasing','-fsanitize=address,undefined',
                '-fno-sanitize-recover=all','-fno-omit-frame-pointer']
            command+=['-I'+str(relative_file(run,p)) for p in job['includes']]
            command+=[str(relative_file(run,p)) for p in job['sources']]
            test=relative_file(APP,job['test'])
            if 'extract' in job:
                ex=job['extract']; data=relative_file(run,ex['source']).read_text()
                section=data[data.index(ex['start']):data.index(ex['end'])]
                test=output/(job['name']+'.cpp')
                test.write_text(relative_file(APP,ex['prefix']).read_text()+section+relative_file(APP,ex['suffix']).read_text())
                command.append('-Wno-unused-const-variable')
            command+=[str(test),'-o',str(binary)]
            with (output/(job['name']+'-compile.log')).open('xb') as log:
                subprocess.run(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,check=True)
            with (output/(job['name']+'-run.log')).open('xb') as log:
                subprocess.run([str(binary), *[a.replace('${output}', str(output)) for a in job.get('run_args', [])]],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
            result['jobs'].append(dict(name=job['name'],binary_sha256=digest(binary)))
        verify_inputs(run)
        result.update(status='PASS',host_execution=True)
    except BaseException as error:
        result.update(status='FAIL',error=str(error))
        (output/'failure.json').write_text(json.dumps(result,indent=2)+'\n')
        raise
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',required=True,type=Path)
    parser.add_argument('--compiler',required=True,type=Path)
    args=parser.parse_args()
    print(json.dumps(execute(args.run,args.compiler),indent=2))
