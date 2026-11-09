"""Replay one original XAM blend dispatcher without executing Xbox machine code.

This deliberately small interpreter observes three device callbacks. It does
not emulate a GPU, arbitrary PowerPC functions, render targets or blend pixels.
Unknown instructions, calls and memory reads fail instead of being guessed.
"""
from pathlib import Path
import argparse
import hashlib
import importlib
import json
import re
import sys


IMAGE_SHA256 = '1733db65e4609b9567ddfc461933e957baac52a4783c0615133db31ab13407fd'
IMAGE_BASE = 0x81870000
START, END = 0x81934C18, 0x81934CD0
SAVE_REGISTERS, RESTORE_REGISTERS = 0x8195EC6C, 0x8195ECBC


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def register(name):
    if not re.fullmatch(r'r(?:[0-9]|[12][0-9]|3[01])', name):
        raise ValueError('Unexpected register: ' + name)
    return int(name[1:])


def replay(program, mode, object_address):
    """Run only the recovered dispatcher, using supplied device callback stubs."""
    stack = 0x70010000
    vtable = object_address + 0x1000
    callbacks = {0x60000100: 'operation', 0x60000200: 'source', 0x60000300: 'destination'}
    memory = {object_address: vtable, object_address + 0xE4: mode & 0xFFFFFFFF,
              vtable + 0xB0: 0x60000100, vtable + 0xA8: 0x60000200,
              vtable + 0xAC: 0x60000300}
    regs = [0xBAD00000 + index for index in range(32)]
    regs[1], regs[3] = stack, object_address
    pc, ctr, equal = START, None, None
    observed, trace = [], []

    for _ in range(128):
        if pc not in program:
            raise ValueError(f'Instruction outside supplied function: {pc:#x}')
        instruction = program[pc]
        op = instruction.mnemonic
        args = instruction.op_str.split(', ') if instruction.op_str else []
        trace.append(f'{pc:08x}: {op} {instruction.op_str}')
        next_pc = pc + 4
        if op == 'mflr':
            regs[register(args[0])] = 0
        elif op == 'bl':
            # This exact compiler helper saves nonvolatile registers to the
            # stack. It neither selects blend state nor calls the device.
            if int(args[0], 0) != SAVE_REGISTERS:
                raise ValueError('Unverified direct call')
        elif op == 'stwu':
            if args != ['r1', '-0x70(r1)']:
                raise ValueError('Unexpected stack write')
            regs[1] -= 0x70
        elif op == 'mr':
            regs[register(args[0])] = regs[register(args[1])]
        elif op == 'li':
            regs[register(args[0])] = int(args[1], 0) & 0xFFFFFFFF
        elif op == 'addi':
            regs[register(args[0])] = (regs[register(args[1])] + int(args[2], 0)) & 0xFFFFFFFF
        elif op == 'lwz':
            location = re.fullmatch(r'(-?(?:0x[0-9a-f]+|\d+))\((r\d+)\)', args[1])
            if not location:
                raise ValueError('Unsupported memory operand')
            address = (regs[register(location[2])] + int(location[1], 0)) & 0xFFFFFFFF
            if address not in memory:
                raise ValueError(f'Unmapped memory read: {address:#x}')
            regs[register(args[0])] = memory[address]
        elif op == 'cmpwi':
            if args[0] != 'cr6':
                raise ValueError('Unexpected condition register')
            equal = regs[register(args[1])] == (int(args[2], 0) & 0xFFFFFFFF)
        elif op in ('beq', 'bne'):
            if args[0] != 'cr6' or equal is None:
                raise ValueError('Uninitialized branch condition')
            if equal == (op == 'beq'):
                next_pc = int(args[1], 0)
        elif op == 'b':
            target = int(args[0], 0)
            if target == RESTORE_REGISTERS:
                if regs[1] != stack:
                    raise ValueError('Unbalanced stack')
                return observed, trace
            next_pc = target
        elif op == 'mtctr':
            ctr = regs[register(args[0])]
        elif op == 'bctrl':
            if ctr not in callbacks or regs[3] != vtable:
                raise ValueError('Unverified device callback')
            observed.append({'callback': callbacks[ctr], 'value': regs[4]})
            # Real callbacks may clobber volatile registers. Poisoning them
            # proves subsequent arguments come from the original reloads and
            # preserved registers, rather than convenient test-stub behavior.
            for index in (0, *range(3, 13)):
                regs[index] = 0xBAD10000 + index
        else:
            raise ValueError('Unsupported instruction: ' + op)
        pc = next_pc
    raise ValueError('Instruction budget exceeded')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--owner-image', type=Path, required=True)
    parser.add_argument('--renderer-header', type=Path, required=True)
    parser.add_argument('--reference-header', type=Path, required=True)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sha(args.owner_image) != IMAGE_SHA256:
        raise ValueError('Original XAM image hash differs from verified owner source')
    sys.path.insert(0, str(args.deps))
    capstone = importlib.import_module('capstone')
    decoder = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    code = args.owner_image.read_bytes()[START - IMAGE_BASE:END - IMAGE_BASE]
    program = {instruction.address: instruction for instruction in decoder.disasm(code, START)}
    if len(program) * 4 != len(code):
        raise ValueError('Incomplete original instruction decode')

    header = args.renderer_header.read_text(encoding='utf-8')
    table = re.search(r'blend_states\[\]\s*=\s*\{(.*?)\};', header, re.S)
    if not table:
        raise ValueError('Current renderer blend table missing')
    states = [tuple(map(int, row)) for row in re.findall(r'\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', table[1])]
    if len(states) != 7:
        raise ValueError('Unexpected renderer table size')

    # These names interpret callback numeric values only. They do not prove
    # the downstream device adapter or GPU rounding. Pin the cached reference.
    if sha(args.reference_header) != '1224073721a11e332dab47e34555a4be6ca9a896ed842915db8e641f391e48c3':
        raise ValueError('Pinned Xenos reference changed')
    reference = args.reference_header.read_text(encoding='utf-8')
    for name, number in {'kZero': 0, 'kOne': 1, 'kSrcColor': 4, 'kSrcAlpha': 6,
                         'kOneMinusSrcAlpha': 7, 'kAdd': 0, 'kSubtract': 1,
                         'kMin': 2, 'kMax': 3}.items():
        if not re.search(r'\b' + name + r'\s*=\s*' + str(number) + r'\b', reference):
            raise ValueError('Reference enum mismatch: ' + name)

    cases = []
    modes = [-2147483648, -1, *range(10), 127, 255, 65535, 2147483647]
    for address in (0x10000000, 0x12345000, 0x20001000, 0x6FFFF000):
        for mode in modes:
            observed, trace = replay(program, mode, address)
            if 1 <= mode <= 6:
                expected = [dict(callback=name, value=value) for name, value in
                            zip(('operation', 'source', 'destination'), states[mode])]
                assert observed == expected, (mode, observed, expected)
            else:
                assert observed == [], (mode, observed)
            cases.append({'mode': mode, 'context_address': hex(address), 'callbacks': observed})
    _, example_trace = replay(program, 2, 0x10000000)
    report = {
        'passed': True, 'cases': len(cases), 'results': cases,
        'owner_image': str(args.owner_image), 'owner_sha256': IMAGE_SHA256,
        'function': hex(START), 'function_bytes_sha256': hashlib.sha256(code).hexdigest(),
        'decoded_instructions': len(program), 'capstone_version': capstone.__version__,
        'renderer_header': str(args.renderer_header), 'renderer_sha256': sha(args.renderer_header),
        'renderer_states': states, 'reference_header_sha256': sha(args.reference_header),
        'reference_commit': '95a5c3ee250f80c3b9d139658649d9ffb6db3eec',
        'multiply_trace': example_trace,
        'mode_zero': 'Dispatcher emits no device calls; inheritance is handled by its caller.',
        'scope': 'Original 1888 XAM numeric device-call dispatch only; no later-family or GPU pixel equivalence claim.',
        'skipped_functions': ['Compiler register-save and restore helpers at the exact pinned addresses.'],
        'runtime_modified': False, 'console_actions': 0,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': True, 'cases': len(cases), 'output': str(args.output)}))


if __name__ == '__main__':
    main()
