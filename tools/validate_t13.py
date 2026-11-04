"""T13 read-only validation plus private evidence logs; no network dependencies."""
from pathlib import Path
import ast
import hashlib
import json
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_era_library import RESOURCES,unpack
proof=ROOT/'private/t13-source'
sources=[]
for build,directory in [(6770,'Blades/Retail/6770'),(9199,'NXE/Retail/9199'),
                        (13604,'Kinect/Retail/13604'),(15574,'Metro/V1/Retail/15574'),
                        (17559,'Metro/V2/Retail/17559')]:
    package=RESOURCES/directory/'dash.xex/dashuisk'
    data=unpack(package.read_bytes())['skin.xur']
    path=proof/str(build)/'skin.xur'
    assert path.read_bytes()==data
    assert b'Loading_Large' in data or 'Loading_Large'.encode('utf-16be') in data
    sources.append({'file':path.relative_to(ROOT).as_posix(),'package':str(package),
                    'member':'skin.xur','sha256':hashlib.sha256(data).hexdigest(),
                    'decoder_error':(path.parent/'decode-error.txt').relative_to(ROOT).as_posix()})
(proof/'source-manifest.json').write_text(json.dumps(sources,indent=2)+'\n',encoding='utf-8')
for name in ['tools/build_small_items.py','tools/implement_t13.py','tools/validate_t13.py','tests/test_small_items.py']:
    ast.parse((ROOT/name).read_text(encoding='utf-8'))
commands=[
    [sys.executable,'-B','tests/test_small_items.py'],
    ['C:/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe',
     '--target=x86_64-linux-android35','-std=c++17','-Wall','-Wextra','-Werror',
     '-fsyntax-only','tests/test_dash.cpp','dash_model.cpp']]
log=[]
for command in commands:
    result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
    log.append({'command':command,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
(proof/'validation.json').write_text(json.dumps(log,indent=2)+'\n',encoding='utf-8')
assert all(row['returncode']==0 for row in log)
print('PASS: source hashes, Loading_Large presence, Python syntax, four asset tests, Android Clang syntax-only')

