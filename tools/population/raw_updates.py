"""Local ZIP/STFS/XEX recovery with original-byte and conversion provenance.

Unknown revisions/payloads remain inventoried. No delta is applied without its
base, no module is executed, and source files/archives are never modified.
"""
from pathlib import Path
import hashlib,json,struct,subprocess,zipfile
from stfs_extract import Stfs
from .storage import Plan,relative_parts
from .xex_tool import ensure_tool

PROJECT=Path(__file__).resolve().parents[2]
MAX_ARCHIVE_BYTES=2*1024**3
MAX_MEMBER_BYTES=512*1024**2


def sha(data):return hashlib.sha256(data).hexdigest()


def xex_metadata(data):
    if len(data)<24 or data[:4]!=b'XEX2':raise ValueError('Incomplete XEX2 header')
    count=struct.unpack_from('>I',data,20)[0]
    if count>4096 or 24+8*count>len(data):raise ValueError('Invalid XEX optional-header table')
    headers=dict(struct.unpack_from('>II',data,24+8*i) for i in range(count))
    result=dict(resource_names=[],execution_version=None,compression=None,encryption=None)
    if 0x40006 in headers:
        offset=headers[0x40006]
        if offset+24>len(data):raise ValueError('Truncated execution ID')
        word=struct.unpack_from('>I',data,offset+4)[0]
        result['execution_version']=dict(major=(word>>28)&15,minor=(word>>24)&15,build=(word>>8)&65535,qfe=word&255)
    if 0x3ff in headers:
        offset=headers[0x3ff]
        if offset+8>len(data):raise ValueError('Truncated file-format header')
        result['encryption'],result['compression']=struct.unpack_from('>HH',data,offset+4)
    if 0x2ff in headers:
        offset=headers[0x2ff]
        if offset+4>len(data):raise ValueError('Truncated resource header')
        size=struct.unpack_from('>I',data,offset)[0]
        if size<4 or (size-4)%16 or offset+size>len(data):raise ValueError('Invalid resource table')
        for at in range(offset+4,offset+size,16):
            name=data[at:at+8].rstrip(b'\0').decode('ascii')
            if len(relative_parts(name))!=1:raise ValueError('Resource filename is not a leaf name')
            result['resource_names'].append(name)
    return result


def read_zip(path,output):
    """CRC-check all members before writing; retain the original ZIP separately."""
    data=Path(path).read_bytes();identity=sha(data);root=Path(output)/identity;plan=Plan(root);plan.protect(Path(path))
    entries=[]
    with zipfile.ZipFile(path) as archive:
        infos=archive.infolist()
        if len(infos)>100000 or sum(i.file_size for i in infos)>MAX_ARCHIVE_BYTES:raise ValueError('Archive exceeds recovery limits')
        for info in infos:
            name=info.filename.replace('\\','/').rstrip('/')
            if not name:continue
            relative_parts(name)
            if (info.external_attr>>16)&0o170000==0o120000:raise ValueError('Symlink ZIP member refused')
            if info.is_dir():plan.add_directory('original/'+name);continue
            if info.file_size>MAX_MEMBER_BYTES:raise ValueError('Oversize archive member: '+name)
            payload=archive.read(info)
            if len(payload)!=info.file_size:raise ValueError('Truncated ZIP member: '+name)
            rel='original/'+name
            plan.add_bytes(rel,payload,dict(kind='original-zip-member',archive_sha256=identity,member=info.filename,conversion=None))
            entries.append(dict(path=rel,source_member=info.filename,bytes=len(payload),sha256=sha(payload),signature=payload[:4].hex()))
    plan.apply()
    return root,identity,entries


def expand_stfs(root,entry):
    source=root/entry['path'];package=Stfs(source);plan=Plan(root);plan.protect(source);entries=[]
    try:
        members=package.files()
        for index,item in enumerate(members):
            name=item['path'].replace('\\','/');relative_parts(name)
            rel='stfs/'+entry['sha256']+'/'+name
            if item['dir']:plan.add_directory(rel);continue
            if item['size']>MAX_MEMBER_BYTES or item['size']>source.stat().st_size:raise ValueError('Invalid STFS member size')
            payload=package.read_file(item)
            if len(payload)!=item['size']:raise ValueError('Truncated STFS member: '+name)
            plan.add_bytes(rel,payload,dict(kind='original-stfs-member',container_sha256=entry['sha256'],member=name,conversion=None))
            entries.append(dict(path=rel,parent=entry['path'],source_member=name,bytes=len(payload),sha256=sha(payload),signature=payload[:4].hex(),stfs_index=index))
        plan.apply();return entries
    finally:package.f.close()


def recover_xex(source,root,tool,locator=None):
    source=Path(source);root=Path(root);data=source.read_bytes()
    record=dict(source=locator or str(source.resolve()),source_sha256=sha(data),resources=[])
    try:
        metadata=xex_metadata(data);record.update(metadata)
        if metadata['compression']==3:
            record['status']='requires-delta-base';return record
        target=root/'resources'/record['source_sha256']
        if target.exists():
            proof=target/'recovery.json'
            if not proof.is_file():raise ValueError('Unverified existing resource output preserved')
            saved=json.loads(proof.read_text(encoding='utf-8'))
            for row in saved['resources']:
                if sha((root/row['path']).read_bytes())!=row['sha256']:raise ValueError('Previously extracted resource changed')
            record=dict(saved,source=record['source']);return record
        target.mkdir(parents=True)
        run=subprocess.run([str(tool),'resources',str(source),str(target)],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (target/'extract.log').write_text(run.stdout,encoding='utf-8')
        if run.returncode:raise ValueError('Source-built XEX backend failed; see '+str(target/'extract.log'))
        expected=set(metadata['resource_names']);actual={p.name for p in target.iterdir() if p.is_file() and p.name!='extract.log'}
        if actual!=expected:raise ValueError('Resource table/output mismatch')
        for resource in sorted(target.iterdir()):
            if resource.name=='extract.log':continue
            payload=resource.read_bytes();record['resources'].append(dict(path=resource.relative_to(root).as_posix(),name=resource.name,bytes=len(payload),sha256=sha(payload),signature=payload[:4].hex(),kind='decoded-original-payload',conversion='original XEX decrypt/decompress; payload bytes unmodified'))
        record['status']='resources-extracted'
        (target/'recovery.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    except (OSError,ValueError,UnicodeError,struct.error) as error:
        record['status']='unresolved';record['error']=str(error)
    return record


def recover_zip(path,output,tool,registry=None):
    output=Path(output).resolve()
    if not output.is_relative_to((PROJECT/'private').resolve()):raise ValueError('Recovered files must stay under project/private')
    root,identity,entries=read_zip(path,output)
    known=json.loads(Path(registry or Path(__file__).with_name('known-updates.json')).read_text(encoding='utf-8'))['archives']
    association=next((row for row in known if row['archive_sha256']==identity),None)
    report=dict(schema=1,source_archive=str(Path(path).resolve()),archive_sha256=identity,association=association,
                originals=entries[:],modules=[],opaque=[],failures=[],revision_mapped=bool(association and association.get('build')))
    all_entries=entries[:]
    for entry in entries:
        signature=bytes.fromhex(entry['signature'])
        if signature in [b'CON ',b'LIVE',b'PIRS']:
            try:
                nested=expand_stfs(root,entry);all_entries+=nested;report['originals']+=nested
            except (OSError,ValueError,IndexError,struct.error) as error:
                report['failures'].append(dict(path=entry['path'],stage='STFS',error=str(error)))
    for entry in all_entries:
        signature=bytes.fromhex(entry['signature'])
        if signature!=b'XEX2':
            if signature not in [b'CON ',b'LIVE',b'PIRS']:report['opaque'].append(dict(path=entry['path'],sha256=entry['sha256'],signature=entry['signature'],status='retained; parser/mapping not established'))
            continue
        record=recover_xex(root/entry['path'],root,tool,entry['path']);report['modules'].append(record)
        if record['status']=='unresolved':report['failures'].append(dict(path=entry['path'],stage='XEX',error=record['error']))
    report['status']='recovered-with-unresolved-inputs' if report['failures'] or report['opaque'] or any(m['status']!='resources-extracted' for m in report['modules']) or not report['revision_mapped'] else 'supported-resources-recovered'
    report['original_count']=len(report['originals']);report['resource_count']=sum(len(m['resources']) for m in report['modules'])
    data=(json.dumps(report,indent=2)+'\n').encode();plan=Plan(root);plan.add_bytes('recovery-report.json',data,dict(kind='generated-recovery-provenance'),generated=True);plan.apply()
    return report


def ingest(paths,output,tool_source):
    tool=ensure_tool(tool_source);reports=[]
    for path in paths:
        if not zipfile.is_zipfile(path):raise ValueError('Unsupported raw input: '+str(path))
        report=recover_zip(path,output,tool);reports.append(report)
        print(Path(path).name+': '+report['status']+'; '+str(report['resource_count'])+' resources',flush=True)
    return reports
