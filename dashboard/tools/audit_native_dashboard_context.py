"""Inventory source-visible native-context assumptions; no console access."""
from pathlib import Path
import hashlib,json,re
root=Path(__file__).resolve().parents[1];out=root/'private/native-dashboard';out.mkdir(exist_ok=True)
sources=list(root.glob('*.cpp'))+list(root.glob('*.hpp'))
for sub in ('runtime','ui','assets'):
    sources+=list((root/sub).rglob('*.hpp'))
sources.append(root.parent/'native-overlay/nxe_bridge.cpp')
records=[]
for source in sorted(set(sources)):
    text=source.read_text(errors='replace')
    matches=[]
    for line_no,line in enumerate(text.splitlines(),1):
        for m in re.finditer(r'"(/(?:data|user|system_data|mnt|app0|system_ex)(?:/[^"\n]*)?)"',line):
            matches.append({'line':line_no,'path_literal':m[1],'dynamic':('%' in m[1] or m[1].endswith('/'))})
    if matches: records.append({'source':str(source.relative_to(root)) if source.is_relative_to(root) else '../native-overlay/nxe_bridge.cpp','sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'paths':matches})
(out/'path-inventory.json').write_text(json.dumps({'scope':'Source literals, including fragments; dynamic paths need concrete runtime values. Presence is not proof of native permission.','files':records},indent=2)+'\n')
print(len(records),'source files;',sum(len(r['paths']) for r in records),'path literals')

# Diagnostic inputs only: never execute or use these strings as write paths.
paths=sorted({item['path_literal'] for record in records for item in record['paths']})
paths=[path for path in paths if '%' not in path and '\\' not in path]
header='#pragma once\nstatic constexpr const char* native_source_paths[]={\n'
header+=''.join('  '+json.dumps(path)+',\n' for path in paths)+'};\n'
(out/'path_inventory.h').write_text(header)
