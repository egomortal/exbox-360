"""Native build verification; does not execute or install the editor.

SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess

LIMITS = [71 << 20, 26 << 20, 2 << 20, 25 << 20, 3 << 20]
HOOKS = ['920BF260', '92166DB0', '920ECC18', '920ECA30', '920FC8A8']


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def layout(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected little-endian ELF64')
    offset = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    if size != 56 or not count or offset + count * size > len(data):
        raise ValueError('Invalid program header table')
    loads = []
    for index in range(count):
        kind, flags, off, va, _, filesz, memsz, align = struct.unpack_from('<II6Q', data, offset + index * size)
        if kind != 1:
            continue
        if filesz > memsz or off + filesz > len(data) or va + memsz > 1 << 64:
            raise ValueError('Invalid LOAD bounds')
        if align > 1 and (align & (align - 1) or off % align != va % align):
            raise ValueError('Invalid LOAD alignment')
        loads.append(dict(index=index, flags=flags, offset=off, address=va,
                          filesz=filesz, memsz=memsz, alignment=align))
    if len(loads) != len(LIMITS):
        raise ValueError('Expected five LOAD segments')
    for index, first in enumerate(loads):
        if max(first['filesz'], first['memsz']) > LIMITS[index]:
            raise ValueError('Unchanged LOAD size limit exceeded')
        for second in loads[index + 1:]:
            for start, length in [('address', 'memsz'), ('offset', 'filesz')]:
                if first[length] and second[length] and max(first[start], second[start]) < min(
                        first[start] + first[length], second[start] + second[length]):
                    raise ValueError('Overlapping LOAD segments')
    return loads


def imports(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected little-endian ELF64')
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count = struct.unpack_from('<HH', data, 58)
    if size != 64 or not count or offset + size * count > len(data):
        raise ValueError('Invalid section header table')
    sections = [struct.unpack_from('<II4QII2Q', data, offset + index * size) for index in range(count)]
    result = {}
    for section in sections:
        if section[1] != 11:
            continue
        if section[6] >= len(sections) or section[9] != 24 or section[5] % 24:
            raise ValueError('Invalid dynamic symbol table')
        strings = sections[section[6]]
        if section[4] + section[5] > len(data) or strings[4] + strings[5] > len(data):
            raise ValueError('Dynamic symbol table outside ELF')
        table = data[strings[4]:strings[4] + strings[5]]
        for pos in range(section[4], section[4] + section[5], 24):
            name, info, _, index, _, _ = struct.unpack_from('<IBBHQQ', data, pos)
            if index != 0 or name == 0:
                continue
            end = table.find(b'\0', name)
            if name >= len(table) or end < 0:
                raise ValueError('Invalid symbol name')
            name = table[name:end].decode('utf-8')
            if name in result:
                raise ValueError('Duplicate undefined symbol')
            result[name] = dict(kind=info & 15, binding=info >> 4, index=index)
    return result


def verify(llvm_elf, native_elf, adapter, link_map, expected_imports, nm, readelf, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    def capture(argv, name):
        value = subprocess.check_output([str(arg) for arg in argv], stderr=subprocess.STDOUT, text=True)
        (output / name).write_text(value, encoding='utf-8')
        return value
    actual = imports(llvm_elf)
    if len(actual) != 441 or actual != expected_imports:
        raise ValueError('Native imports differ from accepted 441-import contract')
    loads = layout(native_elf)
    public = capture([nm, '-g', llvm_elf], 'symbols.txt')
    edge = capture([nm, '-g', adapter], 'adapter-symbols.txt')
    relocations = capture([readelf, '-rW', adapter], 'adapter-relocations.txt')
    sections = capture([readelf, '-SW', adapter], 'adapter-sections.txt')
    if re.search(r'\.(?:tdata|tbss)\b', sections) or re.search(r'\bU (__atomic_|__tls_get_addr)', edge):
        raise ValueError('Unexpected try-on runtime or thread-local dependency')
    mapping = Path(link_map).read_text()
    for hook in HOOKS:
        checks = [len(re.findall(r'\bT sub_' + hook + r'$', public, re.M)) == 1,
                  len(re.findall(r'\bT sub_' + hook + r'$', edge, re.M)) == 1,
                  len(re.findall(r'\bU __imp__sub_' + hook + r'$', edge, re.M)) == 1,
                  bool(re.search(r'__imp__sub_' + hook + r'\b', relocations)),
                  len(re.findall(r'^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s+__imp__sub_' + hook + r'$', mapping, re.M)) == 1]
        if not all(checks):
            raise ValueError('Five-hook binding verification failed: ' + hook)
    result = dict(status='PASS', scope='native imports, layout and hook binding', imports=441,
                  limits_MiB=[limit >> 20 for limit in LIMITS], loads=loads,
                  llvm_elf_sha256=digest(llvm_elf), native_elf_sha256=digest(native_elf),
                  adapter_sha256=digest(adapter), console_contact=False)
    (output / 'native-verification.json').write_text(json.dumps(result, indent=2) + '\n')
    return result
