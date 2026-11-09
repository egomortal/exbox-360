"""Shared pieces for composing an era's Guide from its decoded scenes."""
import sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));sys.path.insert(0,str(HERE.parent))
import xml.etree.ElementTree as ET
from guide_curves import convert_guide,prepare_pending,write_audit,write_xml
import compile_xui_scene
from compile_xui_scene import Compiler
class C(Compiler):
    def prepare_curves(self):prepare_pending(self)
    def properties(self,element):
        values=Compiler.properties(self,element)
        # XUI's own default text colour is 0xff0f0f0f (the dash scenes always set one).
        if element.find('Properties/TextColor') is None:values[17]=self.val(17,'0xff0f0f0f')
        return values
    def add(self,element,*a,**k):
        index=Compiler.add(self,element,*a,**k)
        # XUI fills are solid unless FillType says gradient (2) or radial (3);
        # later skins keep an unused gradient under a solid fill.
        if element.tag=='XuiFigure' and element.find('Properties/Fill/Properties/Gradient') is not None:
            ft=element.findtext('Properties/Fill/Properties/FillType')
            if ft in (None,'1') and self.nodes[index]['meta'][9] in (2,3):self.nodes[index]['meta'][9]=1
        if element.tag=='XuiNineGrid':
            m=[float(element.findtext('Properties/'+n) or 0) for n in ('Left','Top','Right','Bottom')]
            self.nodes[index]['meta'][1]=6;self.nodes[index]['values'][31]=(m,0)
        return index
    def val(self,field,value):
        v,u=Compiler.val(self,field,value);return v,u&0xffffffff
src=open(HERE.parent/'compile_xui_scene.py').read()
body=src[src.index('    def compile(self,main):'):src.index('    def write(self,path):')]
body=body.replace("scene=root.find('XuiTabScene')","scene=root.find('XuiScene')")
# T17: validate used dependencies and audit explicit legacy approximations.
body=body.replace('        for owner,timelines in self.pending:',
                  '        self.prepare_curves()\n        for owner,timelines in self.pending:')
ns={};exec('class _P:\n'+body,{'ET':ET,'FIELDS':compile_xui_scene.FIELDS},ns)
C.compile=ns['_P'].compile
class Pack:
    def __init__(self,source,xml_dir):
        self.F=Path(source);self.tmp=Path(xml_dir);self.tmp.mkdir(parents=True,exist_ok=True);self.sources=[]
    def load(self,rel,name=None):
        root,_=convert_guide(str(self.F/rel));name=name or Path(rel).stem+'.xui'
        colour_factor(root)
        write_xml(root,self.tmp/name);self.sources.append((self.tmp/name,self.F/rel));return root
    @staticmethod
    def find(root,ident,tags=None):
        return next((e for e in root.iter() if e.findtext('Properties/Id')==ident and (tags is None or e.tag in tags)),None)
    @staticmethod
    def scene_children(root):
        sc=root.find('XuiScene');return [c for c in list(sc) if c.tag.startswith('Xui')],sc
    def compile(self,main_root,out):
        write_xml(main_root,self.tmp/'guide-main.xui')
        c=C(self.sources);c.compile(self.tmp/'guide-main.xui')
        print('missing',sorted(c.missing),'unhandled',sorted(c.unhandled))
        c.write(Path(out));write_audit(c,out);return c
def element(tag,parent=None,**props):
    e=ET.Element(tag) if parent is None else ET.SubElement(parent,tag);pp=ET.SubElement(e,'Properties')
    for k,v in props.items():ET.SubElement(pp,k).text=str(v)
    return e

def inline_xur_images(root,load,folder):
    """Images that are scenes (the battery gauges): replace each with a group
    holding that scene's figures. `load(rel)` converts and registers a scene."""
    for parent in list(root.iter()):
        for i,e in enumerate(list(parent)):
            path=e.findtext('Properties/ImagePath') or ''
            if e.tag=='XuiImage' and path.endswith('.xur'):
                g=ET.Element('XuiGroup');pr=e.find('Properties');pr.remove(pr.find('ImagePath'));g.append(pr)
                sub=load(folder+'/'+path)
                inner=sub.find('XuiScene') if sub.find('XuiScene') is not None else sub
                for c in list(inner):
                    if c.tag.startswith('Xui'):g.append(c)
                parent.remove(e);parent.insert(i,g)

IMAGE_TAGS=('XuiImage','XuiImagePresenter','XuiNineGrid')
def colour_factor(root):
    """XUIB 8's element colour factor (Element25) tints images: carried in the
    image's DropShadowColor slot, which images otherwise never use."""
    for owner in root.iter():
        kids={c.findtext('Properties/Id'):c for c in owner if c.tag.startswith('Xui')}
        if owner.tag in IMAGE_TAGS:
            e=owner.find('Properties/Element25')
            if e is not None:e.tag='DropShadowColor'
        for t in owner.findall('Timelines/Timeline'):
            target=kids.get(t.findtext('Id'))
            if target is not None and target.tag in IMAGE_TAGS:
                for p in t.findall('TimelineProp'):
                    if p.text=='Element25':p.text='DropShadowColor'
