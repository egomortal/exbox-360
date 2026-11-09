"""Compose the NXE Guide (9199): xam hudbkgnd, hud GuideMain with its five tab
scenes, the Mini Media Player, battery scenes inlined, skinned by huduiskin."""
import sys,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from guide_pack import Pack,ET,element
src=os.environ.get('GUIDE_NXE_SOURCE','guide-unpacked/2.0.9199.0/u')
out=Path(sys.argv[1]);p=Pack(src,out.parent/(out.stem+'-xml'))
skin=p.load('huduiskin/skin/skin.xur','skin.xui')
bk=p.load('huduiskin/xam/hudbkgnd.xur','hudbkgnd.xui')
host=p.find(bk,'AppHostElementId')
main=p.load('hud/hud/GuideMain.xur')
kids,_=p.scene_children(main)
hud=element('XuiScene',host,Id='HUDScene',Width=852,Height=480)
for k in kids:hud.append(k)
# The HUDScene's own timelines (1Open...) live on the scene element.
hs=main.find('XuiScene')
if hs.find('Timelines') is not None:hud.append(hs.find('Timelines'))
tabs={'Tab1':'hud/hud/MarketplaceTab.xur','Tab2':'hud/hud/GamesTabSignedIn.xur','Tab3':'hud/hud/HomeTabSignedIn.xur','Tab5':'hud/hud/SettingsTabSignedIn.xur'}
for tid,rel in tabs.items():
    t=p.find(bk,tid,('XuiScene',));sub=p.load(rel)
    ch,sc=p.scene_children(sub)
    for c in ch:t.append(c)
    if sc.find('Timelines') is not None:t.append(sc.find('Timelines'))
music=p.find(bk,'scnMusic',('XuiScene',));mm=p.load('hud/hud/MiniMediaPlayer.xur')
for c in p.scene_children(mm)[0]:music.append(c)
# Images that are scenes (the battery gauges): inline the scene's figures.
for parent in list(bk.iter()):
    for i,e in enumerate(list(parent)):
        if e.tag=='XuiImage' and (e.findtext('Properties/ImagePath') or '').endswith('.xur'):
            rel='hud/hud/'+e.findtext('Properties/ImagePath');g=ET.Element('XuiGroup');g.append(e.find('Properties'))
            g.find('Properties').remove(g.find('Properties/ImagePath'))
            sub=p.load(rel)
            inner=sub.find('XuiScene') if sub.find('XuiScene') is not None else sub
            for c in list(inner):
                if c.tag.startswith('Xui'):g.append(c)
            parent.remove(e);parent.insert(i,g)
# Ex-Box's own rows: seven per tab in the tab's geometry (315 x 30 every 27,
# XuiButtonGuide), over the tab's original buttons, which are kept but hidden.
for n in range(1,6):
    t=p.find(bk,'Tab%d'%n,('XuiScene',))
    for c in list(t):
        if c.tag.startswith('Xui'):
            pr=c.find('Properties');sh=pr.find('Show')
            if sh is None:sh=ET.SubElement(pr,'Show')
            sh.text='false'
    for r in range(7):element('XuiNavButton',t,Id='row%d'%r,Width=315,Height=30,Position='0,%d,0'%(27*r),Visual='XuiButtonGuide',Text='')
# The painter treats ids Tab1..Tab6 as the dashboard's native placeholders; rename.
for e in bk.iter():
    for tag in ('Properties/Id','Id'):
        x=e.find(tag)
        if x is not None and x.text in ('Tab1','Tab2','Tab3','Tab4','Tab5'):x.text='Guide'+x.text
p.compile(bk,out)
