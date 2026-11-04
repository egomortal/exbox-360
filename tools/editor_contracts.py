# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate factual hook contracts before any executable adapter is generated.

This module does not read or copy a reference implementation. Unverified entries
remain useful documentation but may never silently become active hooks.
"""
import re

REQUIRED = ("binary", "registers", "memory", "continuation", "trace", "parity")

def activation_plan(entries):
    """Return ordered, verified contracts; fail closed on ambiguity or evidence gaps."""
    seen_ids = set()
    groups = {}
    for entry in entries:
        name = entry["id"]
        if name in seen_ids:
            raise ValueError("duplicate contract id: " + name)
        seen_ids.add(name)
        pc = entry["pc"]
        if not isinstance(pc, int) or not 0 <= pc <= 0xffffffff or pc % 4:
            raise ValueError("invalid aligned guest PC: " + name)
        if not re.fullmatch(r"[0-9a-f]{64}", entry["xex_sha256"]):
            raise ValueError("exact XEX identity required: " + name)
        if not entry.get("enabled", False):
            continue
        evidence = entry.get("evidence", {})
        if any(not isinstance(evidence.get(key), str) or not evidence[key].strip()
               for key in REQUIRED):
            raise ValueError("missing activation evidence: " + name)
        groups.setdefault((entry["xex_sha256"], pc), []).append(entry)
    result = []
    for key, group in sorted(groups.items()):
        if len(group) > 1:
            order = [entry.get("order") for entry in group]
            traces = {entry.get("order_trace") for entry in group}
            if (any(type(n) is not int for n in order) or
                    sorted(order) != list(range(len(group))) or
                    len(traces) != 1 or not next(iter(traces))):
                raise ValueError("shared PC requires contiguous order and shared trace: " + hex(key[1]))
            group = sorted(group, key=lambda entry: entry["order"])
        result.extend(group)
    return result
