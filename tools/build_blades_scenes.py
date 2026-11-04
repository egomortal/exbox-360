"""Rebuild both decoded Blades source families from the shared owner asset cache."""
from pathlib import Path
import argparse
from compile_xui_scene import Compiler
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'private/era-skins/blades/scene')
a=p.parse_args();assets=a.assets.resolve()
for build,main in [(1888,'blades-main.xui'),(6717,'blades-main-6717.xui')]:
    source=assets/'360dasharchive/unpacked/Blades/Retail'/str(build)
    xml=assets/'scene-audit'
    compiler=Compiler([(xml/f'blades-{build}.xui',source/'dashskn1/BladeSkin.xur'),
        (xml/f'blades-controls-{build}.xui',source/'dashuisk/skin.xur'),
        (xml/main,source/'dashmain/dashmain.xur')]).compile(xml/main)
    if compiler.missing or compiler.unhandled:raise RuntimeError((compiler.missing,compiler.unhandled))
    compiler.write(a.output/f'{build}.nxui')
