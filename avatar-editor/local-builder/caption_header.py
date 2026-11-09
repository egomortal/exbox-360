"""Derive the caption header locally; the download contains no title strings.

SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import json
from source_patch import relative_file, sha256


def generate(image_path, run, contract_path=None):
    contract = json.loads(Path(contract_path or Path(__file__).with_name('caption-contract.json')).read_text())
    image_path, run = Path(image_path).resolve(strict=True), Path(run).resolve(strict=True)
    data = image_path.read_bytes()
    if contract.get('schema') != 1 or sha256(data) != contract['image_sha256']:
        raise ValueError('Unsupported original loaded image')
    lines = ['// Original 12581 Marketplace captions; original bytes and provenance kept here.',
             'static constexpr const char16_t* kOfflineStoreCaptions[]={']
    if len(contract['entries']) != 11:
        raise ValueError('Expected eleven caption entries')
    for index, item in enumerate(contract['entries']):
        start, length = item['offset'], item['bytes']
        if start < 0 or length < 0 or start + length > len(data):
            raise ValueError('Caption resource lies outside image')
        raw = data[start:start + length]
        if sha256(raw) != item['sha256']:
            raise ValueError('Caption resource hash differs')
        text = raw.decode(item.get('encoding') or ('utf-16-be' if item['endian'] == 'big' else 'utf-16-le'))
        if any(ord(character) > 0xffff for character in text):
            raise ValueError('Unexpected caption encoding')
        ending = '",' if index + 1 < len(contract['entries']) else '"'
        lines.append(' u"' + ''.join('\\u%04x' % ord(character) for character in text) + ending)
    lines.append('};')
    result = (contract['newline'].join(lines) + contract['newline']).encode('utf-8')
    if sha256(result) != contract['output_sha256']:
        raise ValueError('Generated caption header differs from accepted source')
    output = relative_file(run, contract['output'])
    if output == image_path:
        raise ValueError('Output cannot replace original image')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as stream:
        stream.write(result)
    return dict(path=contract['output'], sha256=sha256(result), bytes=len(result))
