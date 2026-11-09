"""Compare visible Metro video pixels with an independent resize of its decoder frame."""
from pathlib import Path
from PIL import Image
import json,math,numpy as np

OUT=Path(__file__).resolve().parent/'build/era-fidelity-pass2/final'
meta=json.loads((OUT/'video-metro-video.json').read_text())
frame=Image.open(OUT/'video-metro-frame.png').convert('RGB')
shot=Image.open(OUT/'video-metro.png').convert('RGB')
x,y,w,h=(meta[k] for k in ['x','y','width','height'])
assert frame.size==(1920,1080) and meta['alpha']==1
assert w>0 and h>0 and abs(w-h*16/9)<2
assert np.asarray(frame).std()>20
reference=frame.resize((w,h),Image.Resampling.LANCZOS)
# The retail-style caption intentionally overlays the bottom of the video.
# Compare the picture above that caption, keeping the complete resize mapping.
visible=h-53-3
actual=shot.crop((x,y,x+w,y+visible))
expected=reference.crop((0,0,w,visible))
a=np.asarray(actual).astype(float);b=np.asarray(expected).astype(float)
delta=np.abs(a-b);mse=float(np.mean((a-b)**2))
psnr=10*math.log10(255**2/mse) if mse else 100
report={'decoder_size':frame.size,'viewport':[x,y,w,h],
 'comparison_region':[x,y,w,visible],'caption_excluded':True,
 'mean_channel_delta':float(delta.mean()),'max_channel_delta':float(delta.max()),
 'psnr_db':psnr,'passed':psnr>45}
(OUT.parent/'video-quality.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['passed'],report
print(json.dumps(report,indent=2))
