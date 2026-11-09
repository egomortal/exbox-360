"""Resolve the documented local input folders without modifying supplied files.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import json
from pathlib import Path

SOURCE_ROLES = ('avatar_editor', 'rexglue_sdk', 'mesa-source',
                'payload-sdk-source', 'vulkan-port-source', 'glm-source',
                'fast-float-source')


def resolve(originals, dependencies, output):
    originals = Path(originals).resolve(strict=True)
    dependencies = Path(dependencies).resolve(strict=True)
    output = Path(output).resolve()
    for root in (originals, dependencies, Path(__file__).resolve().parent):
        if output == root or output.is_relative_to(root):
            raise ValueError('Input map output must be outside originals, dependencies and app')
    sources = {role: dependencies / 'sources' / role for role in SOURCE_ROLES}
    sources['native-sdk'] = dependencies / 'native-sdk'
    original_files = {'xex': originals / 'avatareditor.xex',
                      'xtt': originals / 'xenonclatin.xtt',
                      'splash': originals / 'splash_360.png'}
    tools = {'rexglue': dependencies / 'host-tools/rexglue',
             'xex_source': dependencies / 'sources/xex-tool',
             'host_cxx': dependencies / 'llvm21/bin/clang++',
             'shader_tools': dependencies / 'host-tools'}
    missing = [str(p) for p in list(sources.values()) + list(original_files.values())
               + list(tools.values()) if not p.exists()]
    if missing:
        raise ValueError('Missing supplied inputs:\n' + '\n'.join(missing))
    # Preserve compiler invocation names: clang++ may intentionally link to clang.
    result = {'schema': 1, 'sources': {k: str(v) for k, v in sources.items()},
              'originals': {k: str(v) for k, v in original_files.items()},
              'tools': {k: str(v) for k, v in tools.items()},
              'dependencies': str(dependencies)}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('originals', 'dependencies', 'output'):
        parser.add_argument('--' + key, required=True, type=Path)
    args = parser.parse_args()
    resolve(args.originals, args.dependencies, args.output)
    print('Local input folders validated; original files remain unchanged.', flush=True)


if __name__ == '__main__':
    main()
