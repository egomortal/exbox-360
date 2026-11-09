# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib,json,shlex,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
out=Path(sys.argv[1]).resolve()
if not out.is_relative_to(ROOT/'private') or not out.relative_to(ROOT/'private').parts[0].startswith('q13-'):
    raise ValueError('Candidate must remain in private/q13-*')
out.mkdir(parents=True,exist_ok=True)
build=ROOT/'private/av2/host-build'
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','av2-rendered'],text=True).splitlines()
compile_rows=[shlex.split(line) for line in commands if ' -c ' in line and '/title-port-source/kernel/xam/xam_loader.cpp' in line]
assert len(compile_rows)==1
compile_cmd=compile_rows[0]
obj=out/'loader.o'
compile_cmd[compile_cmd.index('-o')+1]=str(obj)
compile_cmd[compile_cmd.index('-c')+1]=str(ROOT/'modules/editor_layer/loader.cpp')
for option in ('-MF','-MT'):
    if option in compile_cmd:compile_cmd[compile_cmd.index(option)+1]=str(obj)+('.d' if option=='-MF' else '')
compile_cmd+=['-isystem',str(ROOT/'private/av2/sdk-host-source/include'),'-Wall','-Wextra','-Werror']
subprocess.run(compile_cmd,cwd=build,check=True)
link=shlex.split(commands[-1]);assert link[:2]==[':','&&'] and link[-2:]==['&&',':'];link=link[2:-2]
old=[i for i,x in enumerate(link) if x.endswith('/xam_loader.cpp.o')];assert len(old)==1
old_object=link[old[0]];link[old[0]]=str(obj)
ref=Path(link[link.index('-o')+1]);ref=ref if ref.is_absolute() else build/ref
before=sha(ref);candidate=out/'q13-loader';link[link.index('-o')+1]=str(candidate)
link += [str(ROOT/'private/q13-input-tap-20261102/trace.o'),'-Wl,--wrap=__imp__XamInputGetKeystrokeEx','-Wl,--wrap=_ZN3av220rendered_script_tickEm','-Wl,--wrap=_ZN8ae_input9OnHostKeyEN3rex2ui10VirtualKeyEbbbb']
subprocess.run(link,cwd=build,check=True)
assert sha(ref)==before
(out/'receipt.json').write_text(json.dumps(dict(reference_sha256=before,candidate_sha256=sha(candidate),replaced_object=old_object,source_sha256=sha(ROOT/'modules/editor_layer/loader.cpp'),reference_unchanged=True),indent=2))
