"""Stage Q8 native dashboard probe locally. Never deploy or contact console."""
from pathlib import Path
import hashlib,json,shutil,subprocess
root=Path(__file__).resolve().parents[1];state=root/'private/native-dashboard';base=root/'private/av2'
app=state/'out/PPSA99735';(app/'sce_sys').mkdir(parents=True,exist_ok=True);(app/'sce_module').mkdir(exist_ok=True)
assert (state/'status.txt').read_text().startswith('compile_link_convert=passed')
if (app/'eboot.bin').exists():
    import datetime
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup=state/'history'/stamp
    shutil.copytree(app,backup/'PPSA99735')
    for current in app.rglob('*'):
        if current.is_file():
            saved=backup/'PPSA99735'/current.relative_to(app)
            assert hashlib.sha256(current.read_bytes()).digest()==hashlib.sha256(saved.read_bytes()).digest()
    for old in (state/'out/package-manifest.json',state/'self-inspect.txt'):
        if old.exists(): shutil.copy2(old,backup/old.name)
tool=base/'title-smoke/host/ps5-native-tool'
from check_native_layout import inspect as inspect_native_layout
layout = inspect_native_layout(state/'eboot.elf', 'dashboard')
(state/'layout-check.json').write_text(json.dumps(layout, indent=2)+'\n')
with (state/'self-create.log').open('w') as log:
    subprocess.run([str(tool),'self','--sign','--in',str(state/'eboot.elf'),'--out',str(app/'eboot.bin'),'--magic','0x1D3D154F'],check=True,stdout=log,stderr=subprocess.STDOUT)
for original,target in [(base/'title-smoke/out/PPSA99733/sce_module/libc.prx',app/'sce_module/libc.prx'),(base/'title-smoke/out/PPSA99733/sce_sys/icon0.png',app/'sce_sys/icon0.png')]:
    shutil.copy2(original,target);assert hashlib.sha256(original.read_bytes()).digest()==hashlib.sha256(target.read_bytes()).digest()
meta=json.loads((base/'title-smoke/out/PPSA99733/sce_sys/param.json').read_text())
meta['titleId']='PPSA99735';meta['contentId']='UP9000-PPSA99735_00-EXBOXNATIVEPROBE1'
meta['localizedParameters']['en-US']['titleName']='Ex-Box 360 (Native Probe)'
meta['kernel']['flexibleMemorySize']=1073741824
(app/'sce_sys/param.json').write_text(json.dumps(meta,indent=2)+'\n')
with (state/'self-inspect.txt').open('w') as log:
    subprocess.run([str(tool),'self','--inspect','--file',str(app/'eboot.bin')],check=True,stdout=log,stderr=subprocess.STDOUT)
files=[{'path':p.relative_to(app).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(app.rglob('*')) if p.is_file()]
record={'title_id':'PPSA99735','provisional':True,'collision_checked':False,'console_tested':False,'kind':'native-context probe; unchanged SDL dashboard','assets':'Existing /data/homebrew/ExBox360 tree, not duplicated','data':'Existing /data/nxe; normal dashboard may write settings/saves','flexible_memory_requested':1073741824,'icon_origin':'reused generated solid-colour smoke icon','source_elf_sha256':hashlib.sha256((state/'eboot.elf').read_bytes()).hexdigest(),'files':files}
(state/'out/package-manifest.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
