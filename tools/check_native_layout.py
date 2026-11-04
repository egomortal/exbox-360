"""Fail closed before signing a native title: ELF64 LOAD geometry and size policy.

Limits are review bounds, not hardware capabilities. Editor growth above the
working dashboard baseline is allowed only by the named, documented policy.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

# Rounded-up maxima from working PPSA99735, ordered by LOAD index.
BASELINE = [14<<20, 17<<20, 1<<20, 1<<20, 1<<20]
POLICIES = {
    "dashboard": (BASELINE, "Working PPSA99735 baseline, rounded to MiB."),
    "smoke": (BASELINE, "Smoke is bounded by the working PPSA99735 baseline."),
    "editor": ([66<<20, 26<<20, 2<<20, 25<<20, 3<<20],
        "12581 generated guest code plus runtime/RADV: ~65 MiB code, unwind/pack "
        "constants ~25 MiB, guest static tables/BSS ~24 MiB, relocation metadata "
        "~3 MiB. Q7c report; local layout allowance, NOT console validation."),
}

def inspect(path, policy):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    entsize, count = struct.unpack_from("<HH", data, 54)
    if entsize != 56 or not count or phoff + count*entsize > len(data):
        raise ValueError("Invalid program-header table")
    loads = []
    for i in range(count):
        kind, flags, off, va, pa, filesz, memsz, align = struct.unpack_from("<II6Q", data, phoff+i*entsize)
        if kind != 1:
            continue
        if filesz > memsz or off+filesz > len(data) or va+memsz > 1<<64:
            raise ValueError(f"LOAD {i}: invalid bounds")
        if align > 1 and (align & (align-1) or off % align != va % align):
            raise ValueError(f"LOAD {i}: invalid alignment")
        loads.append(dict(index=i, flags=flags, offset=off, address=va, filesz=filesz, memsz=memsz, alignment=align))
    limits, reason = POLICIES[policy]
    if len(loads) != len(limits):
        raise ValueError(f"Expected {len(limits)} LOADs; got {len(loads)}; review layout policy")
    for i, a in enumerate(loads):
        for b in loads[i+1:]:
            for start, size in (("address", "memsz"), ("offset", "filesz")):
                if a[size] and b[size] and max(a[start], b[start]) < min(a[start]+a[size], b[start]+b[size]):
                    raise ValueError(f"LOAD {a['index']} overlaps LOAD {b['index']} in {start}")
        if max(a['filesz'], a['memsz']) > limits[i]:
            raise ValueError(f"LOAD {a['index']} exceeds {policy} size limit {limits[i]}; recorded review required")
    return dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(), policy=policy,
                size_reason=reason, limits=limits, loads=loads, passed=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--policy", required=True, choices=POLICIES)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = inspect(args.elf, args.policy)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"Native layout rejected: {error}\n")
    output = json.dumps(report, indent=2)+"\n"
    if args.report:
        args.report.write_text(output)
    print(output, end="")
