"""Compare legacy/migrated conversions with the independent native source painter."""
from pathlib import Path
import argparse,collections,contextlib,copy,hashlib,importlib.util,inspect,io,json,subprocess,xml.etree.ElementTree as ET
import numpy as np
from PIL import Image
from compile_xui_scene import Compiler
from xui_material import control

CASES=[('row-normal','btn_1line_icon',14,(584,74)),('row-focus','btn_1line_icon',27,(584,74)),('profile','btn_Gamercard',10,(584,208)),('tray-normal','btn_tray',10,(1190,105)),('tray-focus','btn_tray',19,(1190,105))]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def linux(path):
 text=str(path.resolve());return '/mnt/'+text[0].lower()+text[2:].replace('\\','/')

def fixture(xml,identity,destination):
 original=next(v for v in ET.parse(xml).getroot().findall('XuiVisual') if v.findtext('Properties/Id')==identity)
 root=copy.deepcopy(original);root.tag='XuiTabScene';removed=[]
 for node in list(root.iter()):
  for child in list(node):
   if child.tag.startswith('Xui') and child.tag not in ('XuiGroup','XuiFigure'):
    removed.append(child.findtext('Properties/Id'));node.remove(child)
 ids={n.findtext('Properties/Id') for n in root.iter()}
 for timelines in root.iter('Timelines'):
  for timeline in list(timelines.findall('Timeline')):
   if timeline.findtext('Id') not in ids:timelines.remove(timeline)
 canvas=ET.Element('XuiCanvas');canvas.append(root);ET.ElementTree(canvas).write(destination,encoding='utf-8',xml_declaration=True)
 return removed

def metrics(candidate,reference):
 delta=np.abs(np.asarray(candidate,dtype=np.int16)-np.asarray(reference,dtype=np.int16));per_pixel=delta.max(axis=2)
 return dict(mean_channel_delta=float(delta.mean()),p95_max_channel_delta=float(np.percentile(per_pixel,95)),pixels_over_8=int((per_pixel>8).sum()),max_channel_delta=int(delta.max()))

def main():
 p=argparse.ArgumentParser();p.add_argument('--assets',type=Path,required=True);p.add_argument('--legacy',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 spec=importlib.util.spec_from_file_location('legacy_xui_material',a.legacy);legacy=importlib.util.module_from_spec(spec);spec.loader.exec_module(legacy)
 work=Path(__file__).resolve().parents[1];records=[]
 for build in (1888,6717):
  xml=a.assets/f'scene-audit/blades-controls-{build}.xui';xur=a.assets/f'360dasharchive/unpacked/Blades/Retail/{build}/dashuisk/skin.xur'
  for name,identity,frame,size in CASES:
   stem=f'{build}-{name}';scene=a.output/(stem+'.xui');omitted=fixture(xml,identity,scene)
   compiler=Compiler([(xml,xur)]).compile(scene)
   assert not compiler.missing and not compiler.unhandled,(compiler.missing,compiler.unhandled)
   pack=a.output/(stem+'.nxui')
   with contextlib.redirect_stdout(io.StringIO()):compiler.write(pack)
   legacy_args={'original_xur':xur} if 'original_xur' in inspect.signature(legacy.control).parameters else {}
   before,_=legacy.control(xml,identity,frame,size,**legacy_args);after,count=control(xml,identity,frame,size,original_xur=xur)
   before.save(a.output/(stem+'-legacy.png'));after.save(a.output/(stem+'-migrated.png'))
   native=a.output/(stem+'-native.bmp')
   run=subprocess.run(['wsl.exe','--cd',str(work),'env','SDL_VIDEODRIVER=dummy','./build/render-xui-material',linux(pack),linux(work/'private/fonts-owner/Convection.ttf'),str(frame),str(size[0]),str(size[1]),'18',linux(native)],capture_output=True,text=True,timeout=60)
   assert run.returncode==0,run.stderr
   ref=Image.open(native).convert('RGB');ref.save(native.with_suffix('.png'))
   background=Image.new('RGBA',after.size,(191,194,196,255))
   old=Image.alpha_composite(background,before).convert('RGB');new=Image.alpha_composite(background,after).convert('RGB')
   old.save(a.output/(stem+'-legacy-grey.png'));new.save(a.output/(stem+'-migrated-grey.png'))
   records.append(dict(build=build,control=name,visual=identity,frame=frame,size=after.size,figures=count,source_xml_sha256=sha(xml),source_xur_sha256=sha(xur),legacy=metrics(old,ref),migrated=metrics(new,ref),native_log=run.stdout,omitted_presenters=omitted,provenance='Original vector conversion; synthetic figure-only verification fixture is derived, not original scene artwork'))
   print(json.dumps(records[-1]),flush=True)
 report=dict(records=records,improved=sum(r['migrated']['mean_channel_delta']<r['legacy']['mean_channel_delta'] for r in records),limitations=['Independent raster edge coverage and supersampling differ; exact pixel equivalence is not claimed.','Only figure materials are compared; text/image presenters remain native.','Nested loop clocks, strokes, textures and destination-dependent blends require the complete source renderer.','Comparison output is separate from staged/live assets.'])
 (a.output/'verification.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
