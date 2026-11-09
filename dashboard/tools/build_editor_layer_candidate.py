# SPDX-License-Identifier: GPL-3.0-or-later
"""Relink an isolated host candidate from an existing verified reference graph.
No configure, codegen or mutation of the original build graph or console outputs.
"""
import argparse,hashlib,json,shlex,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(path):
    with path.open("rb") as f:return hashlib.file_digest(f,"sha256").hexdigest()
def build(source,output,symbols):
    output=output.resolve();output.parent.mkdir(parents=True,exist_ok=True)
    builddir=ROOT/"private/av2/host-build"
    lines=subprocess.check_output(["ninja","-C",str(builddir),"-t","commands","av2-rendered"],text=True).splitlines()
    tokens=shlex.split(lines[-1]);assert tokens[:2]==[":","&&"] and tokens[-2:]==["&&",":"]
    command=tokens[2:-2];assert command.count("-o")==1
    original=Path(command[command.index("-o")+1]);original=original if original.is_absolute() else builddir/original
    before=digest(original);assert output!=original.resolve()
    obj=output.with_suffix(".o")
    subprocess.run([command[0],"-std=c++23","-O2","-g","-Wall","-Wextra","-Werror","-msse4.1","-mcmodel=large","-I"+str(ROOT/"private/av2/sdk-host-source/include"),"-I"+str(ROOT/"private/av2/sdk-host-source/thirdparty/simde"),"-c",str(source.resolve()),"-o",str(obj)],check=True)
    command[command.index("-o")+1]=str(output)
    command.extend([str(obj)]+["-Wl,--wrap="+name for name in symbols])
    subprocess.run(command,cwd=builddir,check=True)
    assert digest(original)==before
    receipt=dict(reference=str(original),reference_sha256=before,candidate=str(output),candidate_sha256=digest(output),source_sha256=digest(source),symbols=symbols,reference_unchanged=True)
    output.with_suffix(".json").write_text(json.dumps(receipt,indent=2))
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("source",type=Path);p.add_argument("output",type=Path);p.add_argument("symbols",nargs="+");a=p.parse_args();build(a.source,a.output,a.symbols)
