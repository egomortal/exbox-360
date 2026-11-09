"""Validate packaged startup artwork with the production PNG loader.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from source_patch import relative_file
from splash_fixtures import generate
APP = Path(__file__).resolve().parent

def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path): return json.loads(Path(path).read_text())

def verify_inputs(run):
    contract=read(APP/'splash-contract.json')
    for name,expected in contract['sources'].items():
        if digest(relative_file(run,name))!=expected:
            raise ValueError('Splash source differs: '+name)
    for name,expected in contract['tests'].items():
        if digest(relative_file(APP,name))!=expected:
            raise ValueError('Splash harness differs: '+name)
    return contract

def payload_hashes(run,contract):
    return {name:digest(relative_file(run,name)) for name in contract['payloads']}

def guard(run):
    run=Path(run).resolve(strict=True)
    contract=verify_inputs(run)
    result=read(run/'splash-behavior/result.json')
    if (result.get('status')!='PASS' or result.get('host_execution') is not True
        or result.get('actual_payload_execution') is not True
        or result.get('contract_sha256')!=digest(APP/'splash-contract.json')
        or result.get('sources')!=contract['sources']
        or result.get('tests')!=contract['tests']
        or result.get('payloads')!=payload_hashes(run,contract)):
        raise ValueError('Splash behavior gate has not passed for these sources and images')
    return result

def execute(run,compiler):
    run=Path(run).resolve(strict=True);contract=verify_inputs(run)
    images=payload_hashes(run,contract)
    output=run/'splash-behavior';output.mkdir(exist_ok=False)
    result=dict(status='RUNNING',host_execution=False,actual_payload_execution=False,
        console_contact=False,sources=contract['sources'],tests=contract['tests'],
        payloads=images,contract_sha256=digest(APP/'splash-contract.json'))
    try:
        fixtures=generate(output/'fixtures')
        loader=relative_file(run,contract['loader'])
        include=relative_file(run,contract['decoder_include'])
        binary=output/'startup-loading-test'
        command=[str(compiler),'-std=c++17','-O2','-g','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer',
            '-I'+str(loader.parent),'-I'+str(include),str(loader),
            str(relative_file(APP,'tests/splash_decoder.cpp')),
            str(relative_file(APP,'tests/startup_loading_test.cpp')),'-o',str(binary)]
        with(output/'compile.log').open('xb')as log:
            subprocess.run(command,check=True,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        with(output/'run.log').open('xb')as log:
            subprocess.run([str(binary),str(fixtures),*[str(relative_file(run,name)) for name in contract['payloads']]],
                check=True,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,timeout=120)
        verify_inputs(run)
        if payload_hashes(run,contract)!=images:
            raise ValueError('Splash images changed during validation')
        result.update(status='PASS',host_execution=True,actual_payload_execution=True,
            binary_sha256=digest(binary),asan_ubsan=True,optimization='O2')
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
