"""Build a source/native comparison gallery from completed local captures."""

from pathlib import Path

import argparse,json, math, shutil

import numpy as np

from PIL import Image



root=Path(__file__).resolve().parents[1]

parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path)

args=parser.parse_args();out=args.output.resolve() if args.output else root/'build/blades-reconstruction-20261025'

reference=root.parent/'era-reference/audit-20261025'

page=['''<!doctype html><meta charset="utf-8"><title>Blades reconstruction comparison</title>

<style>body{background:#181b1d;color:#eceee8;font:16px system-ui;margin:28px}h1{font-size:28px}p{max-width:1100px;line-height:1.55}a{color:#b7e989}.pair{display:grid;grid-template-columns:1fr 1fr;gap:14px}img{width:100%;display:block}figure{margin:0}figcaption{padding:9px;background:#303638}section{margin:35px 0}nav{display:flex;gap:20px;flex-wrap:wrap}details{padding:12px;background:#242a2b;margin:10px 0}</style>

<h1>Blades — original / native comparison</h1>

<p>Local desktop renders only. The original captures are the visual target. Native actions differ: installed titles and settings are real local functions. Button slots follow the original arrangement even when their action labels change. This remains a reconstruction; animated materials, early rails, Guide and nested pages are not claimed to be exact.</p>

<nav><a href="#games">Games</a><a href="#live">Xbox Live</a><a href="#media">Media</a><a href="#system">System</a><a href="#side-labels">Side label spacing</a><a href="#typography">Typography</a><a href="#motion">Shine phases</a><a href="catalog/index.html">16 build captures</a><a href="color-review.json">Color samples</a><a href="video-quality.json">Video comparison</a><a href="BLADES_MATERIALS.md">Recovered material data</a><a href="BLADES_SOURCE_DATA.md">Original data / importer coverage</a></nav>''']

colors=[]

for native,source,title in [('games','games','Games'),('live','live','Xbox Live'),('media','music','Media'),('system','system','System')]:

    original=reference/f'designer-blades-{source}.jpg'

    copied=out/f'original-{native}.jpg';shutil.copy2(original,copied)

    after=out/f'blades-1888-{native}.png'

    a=np.asarray(Image.open(original).convert('RGB').resize((1920,1080),Image.Resampling.LANCZOS))

    b=np.asarray(Image.open(after).convert('RGB'))

    points=[]

    # Clean patches visible in both compositions. These are calibration checks,

    # not a whole-frame similarity score across different native menu content.

    for x,y in [(900,50),(1450,650),(1100,990)]:

        av=np.median(a[y-4:y+5,x-4:x+5],axis=(0,1)).astype(int)

        bv=np.median(b[y-4:y+5,x-4:x+5],axis=(0,1)).astype(int)

        points.append(dict(x=x,y=y,original=av.tolist(),native=bv.tolist(),mean_channel_delta=float(np.abs(av-bv).mean())))

    colors.append(dict(blade=native,patches=points))

    page.append(f'<section id="{native}"><h2>{title} · four blades</h2><div class="pair"><figure><a href="{copied.name}"><img src="{copied.name}"></a><figcaption>Original designer capture</figcaption></figure><figure><a href="{after.name}"><img src="{after.name}"></a><figcaption>Native local render · click for full resolution</figcaption></figure></div></section>')

    previous=root/'build/blades-reconstruction-20261025'/after.name

    if previous.exists() and previous.parent!=out:

        old=out/f'previous-{native}.png';shutil.copy2(previous,old)

        page.append(f'<details><summary>Previous local {title} render</summary><a href="{old.name}"><img loading="lazy" src="{old.name}"></a></details>')

if (out/'typography/rail-spacing.json').exists():
    page.append('<section id="side-labels"><h2>Side label margins — before / after</h2><p>Visible lettering is centered inside each mapped blade face. Transparent font padding is removed without resampling. Text follows each blade notch instead of sharing one vertical position.</p><a href="typography/rail-spacing.png"><img src="typography/rail-spacing.png"></a><p><a href="typography/rail-spacing.json">41 slot margin checks</a> · <a href="rail-layout.tsv">Native geometry table</a> · <a href="typography/original-label-geometry.json">Original XUI dimensions and timelines</a></p></section>')
if (out/'typography/metrics.json').exists():

    page.append('<section id="typography"><h2>Typography — original and finished native text</h2><p>Menu text, headings, vertical names, profile and footer labels have separate role sizes and placement. These samples measure ink in the finished native frame against normalized original captures. Compressed reference pixels are not a claim of exact font rasterization.</p><a href="typography/comparison.png"><img src="typography/comparison.png"></a><p><a href="typography/metrics.json">Measured word sizes and positions</a> · <a href="BLADES_TYPOGRAPHY.md">Font and role findings</a></p></section>')

if (out/'motion/results.json').exists():

    page.append('<section id="motion"><h2>Recovered moving shine — sampled phases</h2><p>Each phase uses a separate native run so writing a screenshot cannot disturb its timing. The reflection moves within the selected control; it is drawn before text and icons.</p>')

    for name,region in [('row',(403,355,1027,469)),('tray',(313,752,1546,907))]:

        page.append('<h3>'+name+'</h3><div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px">')

        for phase in ['before','peak','after']:

            source=out/f'motion/{name}-{phase}.png';crop=out/f'motion/{name}-{phase}-crop.png'

            Image.open(source).crop(region).save(crop)

            page.append(f'<figure><a href="motion/{source.name}"><img src="motion/{crop.name}"></a><figcaption>{phase}</figcaption></figure>')

        page.append('</div>')

    page.append('</section>')

page.append('<section><h2>Five-blade layouts and library control</h2>')

for name in ['blades-marketplace','blades-live','blades-games','blades-media','blades-system','blades-tray','blades-tray-return']:

    page.append(f'<details><summary>{name}</summary><a href="{name}.png"><img loading="lazy" src="{name}.png"></a></details>')

page.append('</section>')

(out/'index.html').write_text('\n'.join(page),encoding='utf-8')

(out/'color-review.json').write_text(json.dumps(dict(note='Selected calibration patches only; not whole-frame pixel equivalence',blades=colors),indent=2)+'\n')



meta=json.loads((out/'video-blades-video.json').read_text())

frame=Image.open(out/'video-blades-frame.png').convert('RGB')

shot=Image.open(out/'video-blades.png').convert('RGB')

x,y,w,h=(meta[k] for k in ['x','y','width','height'])

assert frame.size==(1920,1080) and meta['alpha']==1

actual=np.asarray(shot.crop((x,y,x+w,y+h))).astype(float)

expected=np.asarray(frame.resize((w,h),Image.Resampling.LANCZOS)).astype(float)

delta=np.abs(actual-expected);mse=float(np.mean((actual-expected)**2))

psnr=10*math.log10(255**2/mse) if mse else 100

report=dict(decoder_size=frame.size,viewport=[x,y,w,h],

    mean_channel_delta=float(delta.mean()),max_channel_delta=float(delta.max()),psnr_db=psnr,passed=psnr>45)

(out/'video-quality.json').write_text(json.dumps(report,indent=2)+'\n')

assert report['passed'],report

print(json.dumps(dict(video=report,color_patches=colors),indent=2))

