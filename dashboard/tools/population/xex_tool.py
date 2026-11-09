"""Build the local XEX analysis backend from provided source, never a download."""
from pathlib import Path
import hashlib,json,os,subprocess
from .storage import Plan

PROJECT=Path(__file__).resolve().parents[2]


def ensure_tool(source,output=None,compiler='g++'):
    source=Path(source).resolve(strict=True)
    if not (source/'LICENSE').is_file():raise ValueError('XEX backend source license is missing')
    files=sorted(p for p in (source/'src').rglob('*') if p.is_file() and p.suffix.lower() in ('.cpp','.h'))
    if not files or not (source/'src/main.cpp').is_file():raise ValueError('XEX backend C++ source is incomplete')
    signature=hashlib.sha256(json.dumps([(p.relative_to(source).as_posix(),hashlib.sha256(p.read_bytes()).hexdigest()) for p in files],separators=(',',':')).encode()).hexdigest()
    output=Path(output or PROJECT/'private/tool-builds/xex'/signature).resolve()
    if not output.is_relative_to((PROJECT/'private').resolve()):raise ValueError('Tool build must stay under project/private')
    snapshot=output/'source';plan=Plan(PROJECT);plan.protect(source)
    for path in files+[source/'LICENSE']:
        plan.add_file((snapshot/path.relative_to(source)).relative_to(PROJECT).as_posix(),path,dict(kind='provided-tool-source',source=str(path)))
    for name in ['README.md','RECOVERY_PATCH.json']:
        path=source/name
        if path.is_file():plan.add_file((snapshot/name).relative_to(PROJECT).as_posix(),path,dict(kind='provided-source-provenance',source=str(path)))
    plan.apply()
    binary=output/('xextool-re.exe' if os.name=='nt' else 'xextool-re');manifest_path=output/'build.json'
    if binary.is_file() and manifest_path.is_file():
        saved=json.loads(manifest_path.read_text(encoding='utf-8'))
        if saved.get('source_signature')==signature and saved.get('selftest')=='pass' and saved.get('binary_sha256')==hashlib.sha256(binary.read_bytes()).hexdigest():return binary
        raise ValueError('Cached source-built backend differs; previous files preserved')
    units=sorted((snapshot/'src').glob('*.cpp'))+sorted((snapshot/'src/crypto').glob('*.cpp'))+sorted((snapshot/'src/compress').glob('*.cpp'))
    command=[compiler,'-std=c++17','-O2','-Wall','-Wextra',*[str(p) for p in units],'-o',str(binary)]
    with (output/'compile.log').open('w',encoding='utf-8') as log:
        run=subprocess.run(command,cwd=snapshot,stdout=log,stderr=subprocess.STDOUT)
    if run.returncode:raise ValueError('XEX backend source build failed: '+str(output/'compile.log'))
    run=subprocess.run([str(binary),'selftest'],cwd=output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'selftest.log').write_text(run.stdout,encoding='utf-8')
    if run.returncode:raise ValueError('XEX backend selftest failed; no originals processed')
    manifest=dict(schema=1,built_from_source=True,source=str(source),source_signature=signature,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),compiler=compiler,command=command,selftest='pass',license_sha256=hashlib.sha256((source/'LICENSE').read_bytes()).hexdigest())
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8');return binary
