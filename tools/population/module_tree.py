"""Recover original module trees and bind exact registered era/revision paths."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
import hashlib,json
from .raw_updates import recover_xex
from .storage import Plan
from .xex_tool import ensure_tool

PROJECT=Path(__file__).resolve().parents[2]


def recover_tree(source,output,tool_source,jobs=2,registry=None,kind='dashboard'):
    source=Path(source).resolve(strict=True);output=Path(output).resolve()
    if not output.is_relative_to((PROJECT/'private').resolve()):raise ValueError('Module recovery must stay under project/private')
    known=json.loads(Path(registry or Path(__file__).with_name('known-modules.json')).read_text(encoding='utf-8'))['modules']
    by_hash={}
    for row in known:
        if (row.get('role')=='original-guide-module') != (kind=='guide'):continue
        by_hash.setdefault(row['sha256'],[]).append(row)
    if kind not in ('dashboard','guide'):raise ValueError('Unknown module-tree kind')
    destination='guide-resources' if kind=='guide' else 'dashboard-resources'
    originals=[];unique={}
    # Restrict traversal to the archived era trees; source control metadata is
    # not an original dashboard asset and is never copied or executed.
    roots=[source/family for family in ['Blades','NXE','Kinect','Metro']] if kind=='dashboard' else list((source/'by-era-and-update' if (source/'by-era-and-update').is_dir() else source).glob('*/*/guide-core'))
    if kind=='guide' and not roots:roots=[source]
    for scan_root in roots:
        for path in sorted(scan_root.rglob('*')):
            if path.is_symlink():raise ValueError('Symlink in original module input')
            if not path.is_file():continue
            if kind=='guide' and path.suffix.lower()!='.xex':continue
            data=path.read_bytes();identity=hashlib.sha256(data).hexdigest()
            row=dict(path=path.relative_to(source).as_posix(),sha256=identity,bytes=len(data),signature=data[:4].hex(),associations=by_hash.get(identity,[]))
            originals.append(row);unique.setdefault(identity,(path,row))
    if not originals:raise ValueError('No original era module files found')
    tool=ensure_tool(tool_source);output.mkdir(parents=True,exist_ok=True);modules={}
    def one(item):
        identity,(path,row)=item
        if bytes.fromhex(row['signature'])==b'XEX2':record=recover_xex(path,output,tool,str(path))
        else:record=dict(source=str(path),source_sha256=identity,status='original-container-retained',resources=[])
        return identity,record
    with ThreadPoolExecutor(max_workers=max(1,min(jobs,4))) as pool:
        for future in as_completed([pool.submit(one,item) for item in unique.items()]):
            identity,record=future.result();modules[identity]=record
            if len(modules)%20==0:print('Recovered '+str(len(modules))+'/'+str(len(unique))+' unique original modules',flush=True)
    plan=Plan(output);plan.protect(source);mappings=[];unresolved=[]
    for original in originals:
        path=source/original['path'];record=modules[original['sha256']]
        if not original['associations']:
            unresolved.append(dict(path=original['path'],reason='unregistered content; payload retained without a guessed revision'))
            continue
        for association in original['associations']:
            if record['status']=='resources-extracted':
                for resource in record['resources']:
                    relative='resolved/'+destination+'/'+association['path']+'/'+resource['name']
                    plan.add_file(relative,output/resource['path'],dict(kind='decoded-original-payload',original_module_sha256=original['sha256'],registered_revision_path=association['path'],conversion='XEX decrypt/decompress; embedded payload unchanged'))
                    mappings.append(dict(original=original['path'],revision_path=association['path'],resource=resource['name'],sha256=resource['sha256'],output=relative))
            elif bytes.fromhex(original['signature'])==b'XUIZ':
                relative='resolved/shared-resources/'+association['path']
                plan.add_file(relative,path,dict(kind='original-container',module_sha256=original['sha256'],conversion=None))
            else:unresolved.append(dict(path=original['path'],revision_path=association['path'],reason=record.get('error',record['status'])))
    report=dict(schema=1,source=str(source),originals=originals,modules=[modules[k] for k in sorted(modules)],mappings=mappings,unresolved=unresolved,
                resource_root=str(output/'resolved'/destination),shared_root=str(output/'resolved/shared-resources'),
                status='recovered-with-unresolved-inputs' if unresolved else 'registered-resources-recovered',
                source_files=len(originals),unique_payloads=len(unique),resolved_resource_bindings=len(mappings))
    plan.add_bytes('module-recovery.json',(json.dumps(report,indent=2)+'\n').encode(),dict(kind='generated-provenance'),generated=True);report['writes']=plan.apply()
    return report
