"""Run hash-pinned source/header producers in a fresh local workspace.
SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
class ProducerError(ValueError):pass
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def inside(root,value):
 if not isinstance(value,str) or Path(value).is_absolute():raise ProducerError('Expected run-relative producer path')
 path=(root/value).resolve()
 if not path.is_relative_to(root):raise ProducerError('Producer path escapes run')
 return path
def produce(contract,run,dependencies):
 run=Path(run).resolve(strict=True);dependencies=Path(dependencies).resolve(strict=True);nodes=contract['nodes'];receipts=[];done=set();receipt_dir=run/'producer-receipts/mesa'
 if receipt_dir.exists():raise ProducerError('Producer receipts already exist; use a fresh run')
 targets=set()
 for node in nodes:
  for out in node['outputs']:
   path=inside(run,out)
   if path in targets or path.exists():raise ProducerError('Producer output collision: '+out)
   if out not in node['output_sha256']:raise ProducerError('Unpinned generated output: '+out)
   targets.add(path)
 receipt_dir.mkdir(parents=True)
 def expand(value):
  value=value.replace('${run}',str(run)).replace('${dependencies}',str(dependencies))
  if '${' in value:raise ProducerError('Unresolved producer variable')
  return value
 try:
  for index,node in enumerate(nodes):
   if not set(node['depends_on'])<=done:raise ProducerError('Producer dependency not complete: '+node['id'])
   cwd=inside(run,node['cwd']);cwd.mkdir(parents=True,exist_ok=True)
   for out in node['outputs']:inside(run,out).parent.mkdir(parents=True,exist_ok=True)
   argv=[expand(x) for x in node['argv']]
   executable=Path(argv[0]).resolve(strict=True)
   if not executable.is_file():raise ProducerError('Missing supplied host tool')
   env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1');env.update(node.get('env',{}))
   log=receipt_dir/(str(index)+'.log');capture=inside(run,node['stdout']) if node.get('stdout') else None
   with log.open('wb') as stream:
    if capture:
     if str(capture.relative_to(run)) not in node['outputs']:raise ProducerError('Undeclared stdout output')
     with capture.open('xb') as sink:result=subprocess.run(argv,cwd=cwd,env=env,stdout=sink,stderr=stream)
    else:result=subprocess.run(argv,cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT)
   if result.returncode:raise ProducerError('Producer '+node['id']+' exited '+str(result.returncode)+'; log '+str(log.relative_to(run)))
   outputs={}
   for out in node['outputs']:
    path=inside(run,out)
    if not path.is_file():raise ProducerError('Missing generated output: '+out)
    actual=sha(path)
    if actual!=node['output_sha256'][out]:raise ProducerError('Generated SHA mismatch: '+out+' expected '+node['output_sha256'][out]+' got '+actual)
    outputs[out]=actual
   done.add(node['id']);receipts.append(dict(id=node['id'],status='PASS',outputs=outputs));(receipt_dir/'progress.json').write_text(json.dumps(receipts,indent=2)+'\n')
 except Exception as error:
  (receipt_dir/'result.json').write_text(json.dumps(dict(status='FAIL',completed=len(done),error=str(error),receipts=receipts),indent=2)+'\n');raise
 result=dict(status='PASS',nodes=len(done),outputs=sum(len(x['outputs']) for x in receipts),receipts=receipts);(receipt_dir/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--contract',required=True);p.add_argument('--run',required=True);p.add_argument('--dependencies',required=True);a=p.parse_args();result=produce(json.loads(Path(a.contract).read_text()),a.run,a.dependencies);print(json.dumps(dict(status=result['status'],nodes=result['nodes'],outputs=result['outputs'])))
