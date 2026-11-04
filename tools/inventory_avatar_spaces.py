"""Inventory owner-supplied avatar-related scenes without schema guesses."""
from pathlib import Path
import sys,json,hashlib,re,datetime
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/xur5'));sys.path.insert(0,str(r/'tools'))
from theme_states import members
from custom_scenes import reader,walk
base=r.parents[1]/'decrypted/resources'
profiles=[('Blades',1888,'Blades/Retail/1888'),('Transitional Blades',5759,'Blades/Retail/5759'),('Transitional Blades',6770,'Blades/Retail/6770'),('NXE',9199,'NXE/Retail/9199'),('Kinect',13604,'Kinect/Retail/13604'),('Metro V1',15574,'Metro/V1/Retail/15574'),('Metro V2',17559,'Metro/V2/Retail/17559')]
pattern=re.compile(r'avatar|gamerpic|gamertile|portrait',re.I)
cache={};records=[];coverage=[];errors=[]
def inspect(data):
 sha=hashlib.sha256(data).hexdigest()
 if sha in cache:return sha,cache[sha]
 try:d=reader(data)
 except Exception as e:return sha,{'decode_error':type(e).__name__+': '+str(e),'candidate_strings':[],'nodes':[]}
 strings=[x for x in d.strings if isinstance(x,str) and pattern.search(x)]
 try:
  tree=d.decode();nodes=[]
  def visit(node,parents):
   props=node.get('props',{});identity=str(props.get('Id',''));cls=node.get('class','')
   if pattern.search(cls+' '+identity+' '+str(props.get('Text',''))+' '+str(props.get('Texture',''))):
    nodes.append({'class':cls,'id':identity,'parents':parents,'properties':props,'timelines':node.get('timelines',{}),'children_classes':[c.get('class') for c in node.get('children',[])]})
   for child in node.get('children',[]):visit(child,parents+[{'class':cls,'id':identity,'properties':{k:v for k,v in props.items() if k in ['Position','Width','Height','Scale','Rotation','Show','Opacity']}}])
  visit(tree,[]);result={'decode_error':None,'candidate_strings':strings,'nodes':nodes}
 except Exception as e:result={'decode_error':type(e).__name__+': '+str(e),'candidate_strings':strings,'nodes':[]}
 cache[sha]=result;return sha,result
for era,build,relative in profiles:
 folder=base/relative/'dash.xex';count=candidates=0
 if not folder.is_dir():errors.append({'build':build,'path':str(folder),'error':'missing source directory'});continue
 for package in sorted(folder.iterdir()):
  if not package.is_file():continue
  data=package.read_bytes()
  if data[:4]!=b'XUIZ':continue
  try:entries=list(members(data))
  except Exception as e:errors.append({'build':build,'path':str(package),'error':str(e)});continue
  for member,data in entries:
   if not member.lower().endswith('.xur'):continue
   count+=1;sha,result=inspect(data)
   if not result['candidate_strings'] and not result['nodes']:continue
   candidates+=1
   records.append({'era':era,'build':build,'source':str(package),'member':member,'sha256':sha,'status':'decoded candidate' if result['decode_error'] is None else 'blocked candidate','evidence':result,'interpretation':'Candidate discovery only: an avatar-related string or button is not proof of an avatar render slot.','who':'unverified','trigger':'unverified','pose':'unverified','camera_lighting':'unverified'})
 coverage.append({'era':era,'build':build,'dashboard_scenes_scanned':count,'avatar_related_candidates':candidates})
 print(build,count,candidates,flush=True)
# System/Guide scenes are separate sources, never silently borrowed across builds.
for era,build,relative in profiles:
 folder=(r.parents[1]/'1888.FS-decrypted/unpacked-corrected') if build==1888 else (r.parents[2]/'Xbox 360 Guide Recovery/guide-unpacked'/('2.0.%d.0'%build)/'u')
 count=candidates=0
 if not folder.is_dir():
  coverage.append({'era':era,'build':build,'guide_scenes_scanned':0,'guide_source_status':'missing matching source','path':str(folder)})
  continue
 for scene in sorted(folder.rglob('*.xur')):
  count+=1;sha,result=inspect(scene.read_bytes())
  if not result['candidate_strings'] and not result['nodes']:continue
  candidates+=1
  records.append({'era':era,'build':build,'source':str(scene),'member':str(scene.relative_to(folder)),'sha256':sha,'status':'decoded candidate' if result['decode_error'] is None else 'blocked candidate','evidence':result,'interpretation':'Candidate only; semantic classification pending.','who':'unverified','trigger':'unverified','pose':'unverified','camera_lighting':'unverified'})
 coverage.append({'era':era,'build':build,'guide_scenes_scanned':count,'avatar_related_candidates':candidates})
 print('Guide',build,count,candidates,flush=True)
out={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'Part A dashboard and Guide sweep; semantic classification pending','coverage':coverage,'candidates':records,'errors':errors}
(r/'private/q11/dashboard-inventory.json').write_text(json.dumps(out,indent=2,default=str)+'\n')
(r/'docs/astra/avatar-spaces.json').write_text(json.dumps(out,indent=2,default=str)+'\n')
print('candidate records',len(records),'unique scenes decoded',len(cache),'errors',len(errors))