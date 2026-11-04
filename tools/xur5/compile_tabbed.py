"""Compose a tabbed Guide (NXE 9199, Kinect 13604, Metro 15574 / 17559): xam hudbkgnd, hud GuideMain with its five tab
scenes, the Mini Media Player, battery scenes inlined, skinned by huduiskin."""
import sys,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from guide_pack import Pack,ET,element
src=os.environ.get('GUIDE_SOURCE',os.environ.get('GUIDE_NXE_SOURCE','guide-unpacked/2.0.9199.0/u'))
out=Path(sys.argv[1]);p=Pack(src,out.parent/(out.stem+'-xml'))
skin=p.load('huduiskin/skin/skin.xur','skin.xui')
bk=p.load('huduiskin/xam/hudbkgnd.xur' if (Path(src)/'huduiskin/xam/hudbkgnd.xur').exists() else 'xam/xam/hudbkgnd.xur','hudbkgnd.xui')
host=p.find(bk,'AppHostElementId')
main=p.load('hud/hud/GuideMain.xur')
kids,_=p.scene_children(main)
hud=element('XuiScene',host,Id='HUDScene',Width=852,Height=480)
for k in kids:hud.append(k)
# The HUDScene's own timelines (1Open...) live on the scene element.
hs=main.find('XuiScene')
if hs.find('Timelines') is not None:hud.append(hs.find('Timelines'))
five=p.find(bk,'Tab5',('XuiScene',)) is not None
# Five blades (NXE, Kinect, Metro V1): Marketplace, Games, Home, Media, Settings.
# Four (Metro V2): Games & Apps, Home, Media, Settings.
tabs=({'Tab1':'hud/hud/MarketplaceTab.xur','Tab2':'hud/hud/GamesTabSignedIn.xur','Tab3':'hud/hud/HomeTabSignedIn.xur','Tab5':'hud/hud/SettingsTabSignedIn.xur'} if five
      else {'Tab1':'hud/hud/GamesTabSignedIn.xur','Tab2':'hud/hud/HomeTabSignedIn.xur','Tab4':'hud/hud/SettingsTabSignedIn.xur'})
for tid,rel in [(t,r) for t,r in tabs.items() if (Path(src)/r).exists()]:
    t=p.find(bk,tid,('XuiScene',));sub=p.load(rel)
    # Keep the child scene's own transform (later builds start at y=1),
    # default focus and timelines instead of flattening its children.
    t.append(sub.find('XuiScene'))
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
# T19: keep the source controls, including their visibility and skin visuals.
# Hidden conditional rows (and btnXamShutdown) remain hidden and unbound.
# The painter treats ids Tab1..Tab6 as the dashboard's native placeholders; rename.
for e in bk.iter():
    for tag in ('Properties/Id','Id'):
        x=e.find(tag)
        if x is not None and x.text in ('Tab1','Tab2','Tab3','Tab4','Tab5'):x.text='Guide'+x.text
p.compile(bk,out)
