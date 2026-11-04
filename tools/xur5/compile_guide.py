"""Compile the 1888 Guide (hudbkgnd + a HUD scene + the huduiskin skin) into an NXUI pack."""
import sys,copy
from pathlib import Path
sys.path.insert(0,str(__import__('pathlib').Path(__file__).resolve().parent));sys.path.insert(0,str(__import__('pathlib').Path(__file__).resolve().parents[1]))
import xml.etree.ElementTree as ET
from toxml import convert
from guide_curves import write_xml
from compile_xui_scene import Compiler
F=Path(__import__('os').environ.get('GUIDE_1888_SOURCE','1888.FS-decrypted/unpacked-corrected'))
out=Path(sys.argv[1]);scenes=sys.argv[2:] or ['hud/MainMenuSignedIn.xur']
tmp=out.parent/'guide-1888-xml';tmp.mkdir(parents=True,exist_ok=True)
def source(rel):
    # 6770's owner recovery keeps module directories; 1888 is already flat.
    module=rel.split('/')[0]
    nested={'skin':'huduiskin','hud':'hud'}.get(module,'xam')
    return F/rel if (F/rel).is_file() else F/nested/rel

def save(src,name):
    root,_=convert(str(source(src)));write_xml(root,tmp/name);return root
skin=save('skin/skin.xur','skin.xui')
bk=save('xam/hudbkgnd.xur','hudbkgnd.xui')
host=next(e for e in bk.iter('XuiElement') if e.findtext('Properties/Id')=='AppHostElementId')
for s in scenes:
    sc=save(s,Path(s).stem+'.xui')
    for child in list(sc):
        if child.tag.startswith('Xui'):host.append(child)
# Sub-scenes the HUD loads into its placeholder scenes.
subs={'scnMusic':'hud/MiniMediaPlayer.xur','scnCommunity':'hud/CommunityBar.xur','ctlGamerCard':'gamercrd/GamerCard.xur'}
extra=[]
for e in [x for x in bk.iter() if x.tag in ('XuiScene','XuiGamerCard')]:
    sid=e.findtext('Properties/Id')
    if sid in subs:
        sub=save(subs[sid],Path(subs[sid]).stem+'.xui');extra.append(subs[sid])
        inner=sub.find('XuiScene')
        if sid=='ctlGamerCard':e.append(inner)
        else:
            for child in list(inner):
                if child.tag.startswith('Xui'):e.append(child)
# A list page in the Personal Settings scene's geometry (Options.xur: Label_Head
# at 144,44; rows 420 x 47 at x 144 from y 105, every 45), for the Guide's other pages.
lst=ET.SubElement(host,'XuiScene');pp=ET.SubElement(lst,'Properties')
for k,v in [('Id','scnList'),('Width','1120'),('Height','770'),('Show','false')]:ET.SubElement(pp,k).text=v
lab=ET.SubElement(lst,'XuiLabel');pp=ET.SubElement(lab,'Properties')
for k,v in [('Id','labelHeading'),('Width','420'),('Height','47'),('Position','144,44,0'),('Visual','Label_Head'),('Text','Personal Settings')]:ET.SubElement(pp,k).text=v
for i in range(10):
    b=ET.SubElement(lst,'XuiNavButton');pp=ET.SubElement(b,'Properties')
    for k,v in [('Id','row%d'%i),('Width','420'),('Height','47'),('Position','144,%d,0'%(105+45*i)),('Visual','XuiButton'),('Text','')]:ET.SubElement(pp,k).text=v
for i,(name,y) in enumerate([('scr_ScrollEndUp',70),('scr_ScrollEndDown',105+45*10)]):
    b=ET.SubElement(lst,'XuiControl');pp=ET.SubElement(b,'Properties')
    for k,v in [('Id','scroll%d'%i),('Width','27'),('Height','27'),('Position','%d,%d,0'%(144+380,y)),('Visual',name)]:ET.SubElement(pp,k).text=v
# Battery gauges drawn as scenes (6770 on): inline them.
inlined=[]
def _load(rel):inlined.append(rel);return save(rel,Path(rel).stem+'.xui')
from guide_pack import inline_xur_images
inline_xur_images(bk,_load,'hud')
extra+=inlined
write_xml(bk,tmp/'guide-main.xui')
class C(Compiler):
    def properties(self,element):
        values=Compiler.properties(self,element)
        # XUI's own default text colour is 0xff0f0f0f (the dash scenes always set one).
        if element.find('Properties/TextColor') is None:values[17]=self.val(17,'0xff0f0f0f')
        return values
    def add(self,element,*a,**k):
        index=Compiler.add(self,element,*a,**k)
        if element.tag=='XuiNineGrid':
            m=[float(element.findtext('Properties/'+n) or 0) for n in ('Left','Top','Right','Bottom')]
            self.nodes[index]['meta'][1]=6;self.nodes[index]['values'][31]=(m,0)
        return index
    def val(self,field,value):
        v,u=Compiler.val(self,field,value);return v,u&0xffffffff
    def compile(self,main):
        root=ET.parse(main).getroot();scene=root.find('XuiScene')
        self.search_roots=[scene,*self.visuals.values()]
        self.add(scene)
        return Compiler.compile_rest(self)
# reuse the base compile body after add(): patch by running base with a renamed tag
src=open(Path(__file__).resolve().parents[1]/'compile_xui_scene.py').read()
body=src[src.index('    def compile(self,main):'):src.index('    def write(self,path):')]
body=body.replace("scene=root.find('XuiTabScene')","scene=root.find('XuiScene')")
ns={};exec('class _P:\n'+body.replace('\n    ','\n    '),{'ET':ET,'FIELDS':__import__('compile_xui_scene').FIELDS},ns)
C.compile=ns['_P'].compile
c=C([(tmp/'skin.xui',source('skin/skin.xur')),(tmp/'hudbkgnd.xui',source('xam/hudbkgnd.xur'))]+[(tmp/(Path(s).stem+'.xui'),source(s)) for s in scenes+extra])
c.compile(tmp/'guide-main.xui')
print('missing',sorted(c.missing),'unhandled',sorted(c.unhandled))
c.write(out)
