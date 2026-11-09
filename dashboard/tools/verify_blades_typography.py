"""Compare identical words in original captures and finished native renders."""
from pathlib import Path
import argparse, hashlib, json, re
import numpy as np
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
dest = out / 'typography'
dest.mkdir(parents=True, exist_ok=True)
reference = root.parent / 'era-reference/audit-20261025'

def bounds(image, region, light):
    x, y, r, b = region
    pixels = np.asarray(image)[y:b, x:r]
    mask = pixels.min(2) > 175 if light else pixels.max(2) < 90
    yy, xx = np.where(mask)
    return [int(xx.min()+x), int(yy.min()+y), int(xx.max()+x+1), int(yy.max()+y+1)]

cases = [
    ('Media heading', 'music', 'media', (254, 43, 344, 72), (422, 72, 575, 119), True),
    ('Music', 'music', 'media', (308, 232, 386, 257), (517, 384, 640, 434), False),
    ('Pictures', 'music', 'media', (308, 276, 410, 301), (517, 457, 690, 508), False),
    ('System heading', 'system', 'system', (298, 42, 405, 73), (503, 72, 685, 119), True),
]
records = []
sheet = Image.new('RGB', (1400, len(cases)*155), '#24292b')
draw = ImageDraw.Draw(sheet)
for index, (label, original, native, source_region, native_region, light) in enumerate(cases):
    source_path = reference / f'designer-blades-{original}.jpg'
    native_path = out / f'blades-1888-{native}.png'
    source = Image.open(source_path).convert('RGB')
    target = Image.open(native_path).convert('RGB')
    sb = bounds(source, source_region, light)
    nb = bounds(target, native_region, light)
    sx, sy = 1920/source.width, 1080/source.height
    scaled = [sb[0]*sx, sb[1]*sy, sb[2]*sx, sb[3]*sy]
    source_size = [scaled[2]-scaled[0], scaled[3]-scaled[1]]
    native_size = [nb[2]-nb[0], nb[3]-nb[1]]
    difference = [native_size[i]-source_size[i] for i in range(2)]
    position_delta = [nb[i]-scaled[i] for i in range(2)]
    records.append(dict(label=label, original_bounds_at_1080=scaled, native_bounds=nb,
        original_ink_size=source_size, native_ink_size=native_size,
        size_delta=difference, position_delta=position_delta))
    for column, (name, image, region) in enumerate([
            ('Original, normalized to 1080p', source.resize((1920,1080), Image.Resampling.LANCZOS), scaled),
            ('Native, rendered at 1080p', target, nb)]):
        x, y, r, b = [int(round(v)) for v in region]
        crop = image.crop((x-8, y-8, r+8, b+8))
        crop = crop.resize((crop.width*2, crop.height*2), Image.Resampling.NEAREST)
        filename = f'{index}-{column}.png'
        crop.save(dest/filename)
        draw.text((column*700+12,index*155+10), f'{label}: {name}', fill='white')
        sheet.paste(crop,(column*700+12,index*155+32))

font = root/'private/fonts-owner/Convection.ttf'
log = (out/'blades-1888-media.log').read_text()
assert 'face=Xbox TC; body=40 heading=49' in log, 'Actual body/heading font load not confirmed'
rail_roles = re.search(r'rail=23pt:(\d+)/(\d+) 21pt:(\d+)/(\d+) 18pt:(\d+)/(\d+)', log)
assert max(abs(v) for item in records for v in item['size_delta']) < 4, records
assert max(abs(v) for item in records for v in item['position_delta']) < 6, records
sheet.save(dest/'comparison.png')
report = dict(
    note='Ink bounds against compressed designer captures; this is not font pixel equivalence.',
    font_file_sha256=hashlib.sha256(font.read_bytes()).hexdigest(),
    runtime_font_confirmed=True, body_pixels=40, heading_pixels=49,
    rail_role_pixels=([int(value) for value in rail_roles.groups()] if rail_roles else 'legacy fixture; not checked here'),
    rail_scope='This test compares body/heading words only. Use the dedicated side-label audit for rail pixels and source regimes.',
    body_tracking_pixels=1, rail_tracking_pixels=(0 if rail_roles else 'legacy fixture'), wrapped_line_pitch=49,
    cache_limit_bytes=4<<20, cache_limit_entries=64, words=records)
(dest/'metrics.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
