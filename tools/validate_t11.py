"""Offline T11 provenance, deployment, text, layout and syntax checks; no dependencies."""
from pathlib import Path
import ast,json,struct,subprocess,sys

ROOT=Path(__file__).resolve().parent.parent
private=ROOT/'private/astra-t11'
checks=0
def check(value,message):
    global checks
    checks+=1
    if not value:raise AssertionError(message)

for name in ['tools/build_t11_resources.py','tools/t11-implement.py','tools/validate_t11.py']:
    ast.parse((ROOT/name).read_text());check(True,name)
for p in (private/'originals').rglob('*'):
    if not p.is_file():continue
    dest=ROOT/p.relative_to(private/'originals')
    before=p.read_bytes();after=dest.read_bytes()
    check((before.count(b'\r\n')==before.count(b'\n'))==(after.count(b'\r\n')==after.count(b'\n')),f'line endings: {dest}')

extracted=json.loads((private/'extracted-manifest.json').read_text())
packages={}
for item in extracted:
    raw=packages.setdefault(item['source'],None)
    if raw is None:raw=packages[item['source']]=Path(item['source']).read_bytes()
    check((ROOT/item['file']).read_bytes()==raw[item['offset']:item['offset']+item['size']],item['file'])
for item in json.loads((private/'staged-manifest.json').read_text()):
    if 'rows' not in item and Path(item['file']).suffix.lower()!='.wav':
        check((ROOT/item['file']).read_bytes()==(ROOT/item['source']).read_bytes(),f"staged copy: {item['file']}")

pages=0;art=set()
for build in [1888,6770,9199,13604,15574,17559]:
    base=ROOT/f'private/era-skins/{build}/welcome'
    rows={}
    for line in (base/'pages.tsv').read_text(encoding='utf8').split('\n'):
        if not line:continue
        fields=line.split('\t');check(len(fields)==4,f'{build}: malformed row')
        key,title,image,body=fields
        check(key not in rows,f'{build}: duplicate {key}')
        check(not any(ord(c)<32 for c in ''.join(fields)),f'{build}: control characters')
        check(bool(title) or key.startswith('tile/') or key=='background',f'{build}: blank title {key}')
        rows[key]=fields;pages+=1
        if image:
            p=base/image;check(p.is_file(),str(p));data=p.read_bytes()
            check(data.startswith((b'\x89PNG\r\n\x1a\n',b'\xff\xd8')),str(p));art.add(str(p))
    check(all('setup/'+str(i) in rows for i in range(5)),f'{build}: setup steps')
    if build in [9199,13604]:
        check(rows['hide'][3].endswith('Welcome Channel.' if build==9199 else 'Startup and Shutdown.'),'hide prompt boundary')
        check(sum(k.startswith('XboxBasics') and not k.endswith('SlotScene') for k in rows)==(8 if build==9199 else 5),'basics pages')
    if build==15574:check(all(rows[k][2] for k in rows if k.startswith('tile/')),'V1 icons')
    if build==17559:check(all(rows[k][2] for k in rows if k.startswith('tile/ico_')),'V2 icons')

# Full v8 decoder, not just the scene's string order, verifies grid and nav geometry.
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'tools/xur5'))
from xur8 import decode
for build,name,pitch,side in [(15574,'Template6',152,148),(17559,'Template6HD',201,197)]:
    tree,_=decode(str(private/f'{build}/epix/{name}.xur'))
    def visit(node):
        props=node.get('props',{});key=props.get('Id','')
        if key in [str(i) for i in range(1,9)]:
            i=int(key)-1;position=props.get('Position',(0,0,0))
            check(position[:2]==((i%4)*pitch,(i//4)*pitch),f'{build}: position {key}')
            check(props['Width']==side and props['Height']==side,f'{build}: size {key}')
        for child in node.get('children',[]):visit(child)
    visit(tree)

tests=(ROOT/'tests/test_dash.cpp').read_text()
for name in ['welcome_channel_and_setting','initial_setup_per_era','metro_settings_tiles_and_navigation']:
    check('static void '+name+'()' in tests and '    '+name+'();' in tests,name)
check('("era-skins","era-skins")' in (ROOT/'deploy.py').read_text(),'existing recursive private deployment')

clang=Path('C:/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe')
command=[str(clang),'--target=x86_64-linux-android28','-std=c++17','-Wall','-Wextra','-Werror','-fsyntax-only','dash_model.cpp','tests/test_dash.cpp']
result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
(private/'syntax-check.txt').write_text(' '.join(command)+'\n'+result.stdout+result.stderr,encoding='utf8')
check(result.returncode==0,'Android clang syntax check (not host/PS5 execution)')
summary={'checks':checks,'extracted_files':len(extracted),'page_records':pages,'referenced_images':len(art),'syntax_only':'Android clang; C++ tests not executed','missing_audio':json.loads((private/'missing-audio.json').read_text())}
(private/'validation.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
print(json.dumps(summary,indent=2))
