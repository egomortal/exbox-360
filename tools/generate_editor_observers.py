# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the isolated observer adapters from the specification's sole table."""
import argparse,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def records(spec):
    block=spec.split("<!-- q13-observer-contracts-start -->")[1].split("<!-- q13-observer-contracts-end -->")[0]
    rows=[]
    for line in block.splitlines():
        if not line.startswith("| 0x"):continue
        pc,regs,handler,legacy,symbol=[part.strip() for part in line.strip("|").split("|")]
        if not re.fullmatch(r"0x[0-9A-Fa-f]{8}",pc) or int(pc,16)%4:raise ValueError("invalid PC")
        registers=regs.split(",")
        if any(not re.fullmatch(r"r(?:[0-9]|[12][0-9]|3[01])",reg) for reg in registers):raise ValueError("invalid register")
        if any(not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*",s) for s in (handler,legacy,symbol)):raise ValueError("invalid identifier")
        rows.append(dict(pc=pc,registers=registers,handler=handler,legacy=legacy,symbol=symbol))
    for field in ("pc","handler","symbol"):
        if len({row[field] for row in rows})!=len(rows):raise ValueError("duplicate "+field)
    if not rows:raise ValueError("no contracts")
    return rows

def generate(spec,out):
    rows=records(spec.read_text());out.mkdir(parents=True,exist_ok=True)
    code=['// SPDX-License-Identifier: GPL-3.0-or-later','// Generated from EDITOR-LAYER-SPEC.md; host candidate only.',
          '#include <rex/ppc/context.h>','namespace av2 { void note_title_hook(const char*); }',
          'namespace exbox::editor {']
    for row in rows:
        code.append('static void '+row['handler']+'() { av2::note_title_hook("'+row['legacy']+'"); }')
    code.append('}')
    for row in rows:
        args=', '.join('PPCRegister&' for _ in row['registers'])
        code.append('extern "C" void '+row['handler']+'('+args+') asm("__wrap_'+row['symbol']+'");')
        code.append('void '+row['handler']+'('+args+') { exbox::editor::'+row['handler']+'(); }')
    (out/'observers.cpp').write_text('\n'.join(code)+'\n')
    (out/'contracts.json').write_text(json.dumps(rows,indent=2))
    return rows
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);a=p.parse_args()
    generate(ROOT/'docs/astra/EDITOR-LAYER-SPEC.md',a.output)
