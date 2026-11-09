"""One identification/staging engine used by setup CLI and the desktop front end."""
from pathlib import Path
import json,os,zipfile
from .storage import Plan
from verified_files import sha256
REGISTRY=Path(__file__).parent

def registries():
    modules={}
    for row in json.loads((REGISTRY/'known-modules.json').read_text())['modules']:
        modules.setdefault(row['sha256'],[]).append(row)
    updates={row['archive_sha256']:row for row in json.loads((REGISTRY/'known-updates.json').read_text())['archives']}
    return modules,updates

def identify(path,modules,updates):
    digest=sha256(path)
    with path.open('rb') as stream:head=stream.read(32)
    if digest in updates:
        row=updates[digest]
        with zipfile.ZipFile(path) as archive:
            if archive.testzip():return dict(status='damaged',reason='ZIP CRC failure',targets=[])
        return dict(status='recognized',kind='update',revision=row['build'],era=row['family'],sha256=digest,targets=['updates/'+digest+'.zip'])
    if digest in modules:
        matches=modules[digest];targets=[]
        for row in matches:
            prefix='guide-source/' if row.get('role')=='original-guide-module' else 'dashboard-source/'
            targets.append(prefix+row['path'])
        return dict(status='recognized',kind='module',sha256=digest,associations=[x['path'] for x in matches],targets=sorted(set(targets)))
    if path.name.lower()=='shrdres.xzp' and head[:4]==b'XUIZ':
        from .archives import ArchiveCache
        ArchiveCache().lookup(path)
        return dict(status='recognized',kind='shared resources',sha256=digest,verification='container parsed; revision not fingerprinted',targets=['shrdres.xzp'])
    ext=path.suffix.lower()
    video=(ext in ('.mp4','.m4v','.mov') and head[4:8]==b'ftyp') or (ext in ('.mkv','.webm') and head[:4]==bytes.fromhex('1a45dfa3')) or (ext=='.wmv' and head[:4]==bytes.fromhex('3026b275'))
    audio=(ext=='.wav' and head[:4]==b'RIFF' and head[8:12]==b'WAVE') or (ext=='.flac' and head[:4]==b'fLaC') or (ext=='.ogg' and head[:4]==b'OggS') or (ext=='.mp3' and (head[:3]==b'ID3' or (len(head)>1 and head[0]==255 and head[1]&224==224)))
    if video or audio:
        kind='trailers' if video else 'music'
        return dict(status='recognized',kind=kind,sha256=digest,verification='container header only; codec validation occurs when imported/played',targets=[kind+'/'+digest+ext])
    if head[:4]==b'XEX2' or ext in ('.zip','.xex','.xzp','.xexp'):
        if ext=='.zip':
            try:
                with zipfile.ZipFile(path) as archive:
                    if archive.testzip():raise ValueError('CRC failure')
            except (ValueError,zipfile.BadZipFile):return dict(status='damaged',sha256=digest,reason='Invalid ZIP/CRC',targets=[])
        return dict(status='unverified',sha256=digest,reason='Unknown fingerprint/revision or missing delta base; preserved at source',targets=[])
    return dict(status='unknown',sha256=digest,reason='No supported identification; source untouched',targets=[])

def files(root):
    count=0
    for base,dirs,names in os.walk(root,followlinks=False):
        for name in dirs+names:
            if (Path(base)/name).is_symlink():raise ValueError('Symlink input: '+str(Path(base)/name))
        for name in sorted(names):
            count+=1
            if count>100000:raise ValueError('Input directory exceeds 100000 files')
            yield Path(base)/name

def intake(paths,destination,check=False,registry=None):
    modules,updates=registry if registry is not None else registries();plan=Plan(destination);records=[]
    for supplied in paths:
        source=Path(supplied).absolute()
        if source.is_symlink():raise ValueError('Symlink input: '+str(source))
        if not source.exists():raise ValueError('Missing input: '+str(source))
        if source.is_dir():
            prepared=None
            if (source/'assets/avatareditor.xex').is_file() and (source/'assets/ae.pack').is_file():
                from .workflow import EDITOR_XEX,EDITOR_PACK
                if sha256(source/'assets/avatareditor.xex')==EDITOR_XEX and sha256(source/'assets/ae.pack')==EDITOR_PACK:prepared='avatar-editor'
            if (source/'NXE/Retail/9199/dash.xex').is_dir():prepared='dashboard-resources'
            if prepared:
                for path in files(source/'assets' if prepared=='avatar-editor' else source):plan.add_file(prepared+'/'+path.relative_to(source).as_posix(),path,dict(kind='owner-prepared-input',source_sha256=sha256(path)))
                records.append(dict(source=str(source),status='recognized',kind=prepared,verification='prepared structure; setup performs complete validation'));continue
            selected=files(source)
        else:selected=[source]
        for path in selected:
            row=identify(path,modules,updates);row['source']=str(path);records.append(row)
            for target in row['targets']:plan.add_file(target,path,dict(kind='owner-original-input',source_sha256=row['sha256'],conversion=None))
    plan.preflight()
    writes=0 if check else plan.apply()
    return dict(status='checked; no copies' if check else 'intake complete',files=records,writes=writes,missing=feature_status(Path(destination)))

def feature_status(root):
    modules,updates=registries();families={'original Blades':1888,'transitional Blades':6770,'NXE':9199,'Kinect':13604,'Metro':15574,'late Metro':17559}
    present=set();guide=False
    for rows in modules.values():
        for row in rows:
            prefix='guide-source' if row.get('role')=='original-guide-module' else 'dashboard-source'
            if (root/prefix/row['path']).is_file() or (root/'dashboard-resources'/row['path']).is_dir():
                if prefix=='guide-source':guide=True
                for name,rev in families.items():
                    if str(rev) in row['path'].split('/') or any(part.startswith(str(rev)+' ') for part in row['path'].split('/')):present.add(name)
    for digest,row in updates.items():
        if (root/'updates'/(digest+'.zip')).is_file():
            for name,rev in families.items():
                if row['build']==rev:present.add(name)
    result={name:('source present; recovery/build may still be needed' if name in present else 'missing mapped module input') for name in families}
    for name,path in [('Guide',root/'guide-source'),('avatar editor',root/'avatar-editor/assets'),('trailers',root/'trailers'),('music',root/'music')]:
        result[name]='input present; validation pending' if path.is_dir() and any(path.iterdir()) else 'missing'
    result['raw-recovery tool source']='present' if (root/'tool-sources/xextool-re').is_dir() else 'missing (needed for raw XEX/update recovery)'
    result['shared artwork']='present' if (root/'shrdres.xzp').is_file() or (root/'dashboard-shared').is_dir() else 'missing'
    return result
