"""Keep supplied shell wrappers working when selected paths contain spaces.

Only local directory aliases are created; supplied tools are never modified.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import copy
import json
import tempfile
from pathlib import Path

ROLES = ('native-sdk', 'payload-sdk', 'llvm21', 'dxc', 'native-tools', 'python', 'host-tools')


def _validate(record):
    root = Path(record['effective_inputs']['dependencies'])
    if root.is_symlink() or not root.is_dir() or any(c.isspace() for c in str(root)):
        raise ValueError('Execution dependency alias directory changed')
    for item in record['aliases']:
        alias = root / item['name']
        if not alias.is_symlink() or alias.resolve(strict=True) != Path(item['target']):
            raise ValueError('Execution dependency alias changed: ' + item['name'])


def prepare(inputs, receipt_path):
    """Return an effective map; the receipt contains private local paths.

    receipt_path must be outside the public download and may be chosen by the
    caller before source staging. Repeated calls reuse only the same input map.
    """
    receipt_path = Path(receipt_path)
    if receipt_path.resolve().is_relative_to(Path(__file__).resolve().parent):
        raise ValueError('Private alias receipts must stay outside the public app')
    if receipt_path.exists():
        record = json.loads(receipt_path.read_text(encoding='utf-8'))
        if record.get('status') != 'PASS':
            raise ValueError('Preserve failed dependency alias attempt; use a fresh receipt')
        if inputs not in (record['original_inputs'], record['effective_inputs']):
            raise ValueError('Dependency alias receipt belongs to different inputs')
        _validate(record)
        return record['effective_inputs']
    original = Path(inputs['dependencies']).resolve(strict=True)
    if not any(character.isspace() for character in str(original)):
        return copy.deepcopy(inputs)
    selected = []
    for name in ROLES:
        path = original / name
        if not path.is_dir():
            raise ValueError('Missing supplied dependency folder: ' + name)
        selected.append((name, path.resolve(strict=True)))
    root = Path(tempfile.mkdtemp(prefix='editor-dependencies-', dir='/tmp'))
    effective = copy.deepcopy(inputs)
    effective['dependencies'] = str(root)
    if 'shader_tools' in effective.get('tools', {}):
        old_shader = Path(effective['tools']['shader_tools']).resolve(strict=True)
        if old_shader == (original / 'host-tools').resolve(strict=True):
            effective['tools']['shader_tools'] = str(root / 'host-tools')
    record = {'schema': 1, 'status': 'STARTED', 'original_inputs': inputs,
              'effective_inputs': effective, 'aliases': [], 'supplied_files_modified': False}
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        for name, target in selected:
            (root / name).symlink_to(target, target_is_directory=True)
            record['aliases'].append({'name': name, 'target': str(target)})
        _validate(record)
        record['status'] = 'PASS'
    except Exception as error:
        record.update(status='FAIL', error=str(error))
        raise
    finally:
        with receipt_path.open('x', encoding='utf-8') as out:
            json.dump(record, out, indent=2)
    return effective


def for_run(inputs, run):
    return prepare(inputs, Path(run) / 'workflow-receipts/dependency-aliases.json')
