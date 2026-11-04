"""Inventory user-object retirement blockers without relinking or modifying packages.

Run from the repository root in WSL. Evidence covers direct link objects and
candidate adapter objects; archive/dynamic symbol resolution still needs a link
trial before declaring the original object removable.
"""
from pathlib import Path
import ast
import hashlib
import json
import re
import shlex
import subprocess
import sys

root = Path.cwd()
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = root / "private/av2/q7-host-save-source/xam_user.cpp"
build = root / "private/av2/host-build"
text = source.read_text()
commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "av2-rendered"], text=True).splitlines()
compile_rows = [shlex.split(row) for row in commands if " -c " in row and "q7-host-save-source/xam_user.cpp" in row]
assert len(compile_rows) == 1
obj = Path(compile_rows[0][compile_rows[0].index("-o") + 1])
if not obj.is_absolute():
    obj = build / obj
link = shlex.split(commands[-1])[2:-2]
objects = []
for token in link:
    if token.endswith(".o"):
        path = Path(token)
        objects.append(path if path.is_absolute() else build / path)
# Read builder literals as data; never execute the builder for an audit.
builder = root / "private/q13-final-user-binding-20261103/build.py"
for node in ast.walk(ast.parse(builder.read_text())):
    if isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value.startswith("private/") and node.value.endswith(".o"):
        objects.append(root / node.value)
objects.extend(builder.parent.glob("*.o"))
objects = sorted({p.resolve() for p in objects if p.exists() and p.resolve() != obj.resolve()})
def symbol_rows(args):
    result = subprocess.run(["nm", *args], text=True, capture_output=True, check=True)
    return result.stdout.splitlines()
definitions = {}
for row in symbol_rows(["-g", "--defined-only", str(obj)]):
    fields = row.split()
    if len(fields) >= 3:
        definitions[fields[-1]] = fields[-2]
incoming = {}
for path in objects:
    for row in symbol_rows(["-u", str(path)]):
        fields = row.split()
        if fields and fields[-1] in definitions:
            incoming.setdefault(fields[-1], []).append(str(path.relative_to(root)))
plain = sorted(re.findall(r"REX_EXPORT_STUB\((__imp__\w+)\)", text))
constant = sorted(re.findall(r"REX_EXPORT_STUB_RETURN\((__imp__\w+),\s*([^)]*)\)", text))
assert len(plain) == 50 and len(constant) == 6
non_export = {name: users for name, users in incoming.items() if not name.startswith("__imp__")}
receipt = {
    "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest(),
    "direct_objects_scanned": len(objects),
    "ordinary_stubs": {"count": len(plain), "contract": "warn; leave PPC registers unchanged", "exports": plain},
    "constant_stubs": {"count": len(constant), "contract": "warn; assign full r3.u64", "exports": dict(constant)},
    "cvar_definitions": re.findall(r"REXCVAR_DEFINE_\w+\((\w+)", text),
    "referenced_definitions": incoming,
    "non_export_dependencies": non_export,
    "limits": ["Static archive and shared-library consumers not exhaustively enumerated", "Weak/template overlaps need link-resolution analysis", "Registered ordinal entry points must remain available after removal"],
    "whole_object_retired": False,
}
(out / "dependencies.json").write_text(json.dumps(receipt, indent=2) + "\n")
lines = ["# User-object retirement audit", "", f"Scanned {len(objects)} direct/candidate object files.", "", "Ordinary stubs preserve registers; do not replace all stubs with success returns.", "Constant-return stubs assign the full return register. Both groups register ordinal entry points.", "", "## Referenced non-export definitions", ""]
for name, users in sorted(non_export.items()):
    lines.append(f"- `{name}`: {len(users)} consumer(s); see JSON for paths.")
lines += ["", "Cvar registrations, codec definitions and ordinal registrations must survive removal.", "This inventory is not a successful removal/link/parity gate."]
(out / "README.md").write_text("\n".join(lines) + "\n")
print(json.dumps({"objects": len(objects), "referenced_definitions": len(incoming), "non_export_dependencies": len(non_export), "ordinary_stubs": len(plain), "constant_stubs": len(constant)}))
