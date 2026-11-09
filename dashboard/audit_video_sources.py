from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json
import subprocess

ROOT=Path(__file__).resolve().parent
OUT=Path(__file__).resolve().parent/'build/audit-20261025/video-sources.json'
def probe(entry):
    path=ROOT/'private/trailers/originals'/entry['file']
    result=subprocess.run(['ffprobe','-v','error','-select_streams','v:0','-show_entries',
        'stream=width,height,r_frame_rate,sample_aspect_ratio,display_aspect_ratio:format=duration','-of','json',str(path)],capture_output=True,text=True,check=True)
    return {'game':entry['game'],'file':entry['file'],**json.loads(result.stdout)}
sources=json.loads((ROOT/'trailer_sources.json').read_text())
with ThreadPoolExecutor(max_workers=4) as pool:
    results=list(pool.map(probe,sources))
OUT.write_text(json.dumps(results,indent=2))
sizes={}
for result in results:
    s=result['streams'][0];key=f"{s['width']}x{s['height']} @ {s['r_frame_rate']} SAR {s.get('sample_aspect_ratio','1:1')}"
    sizes[key]=sizes.get(key,0)+1
print(json.dumps(sizes,indent=2))
