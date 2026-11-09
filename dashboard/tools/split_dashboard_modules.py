"""Move visual implementations into named files, verifying a lossless source split."""
from pathlib import Path
import re,json,hashlib

ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'main.cpp';original=path.read_text();source=original;records=[]
token=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
def block(text,signature):
    start=text.index(signature);depth=0;opened=False
    for match in token.finditer(text,start):
        if match.group()=='{':depth+=1;opened=True
        elif match.group()=='}':
            depth-=1
            if opened and depth==0:return start,match.end()
    raise ValueError('Unclosed C++ block: '+signature)

groups=[
 ('assets/promotions.inc.hpp',['void load_promotions() {']),
 ('ui/era_art.inc.hpp',['void era_art(', 'SDL_Rect solid_icon_source(', 'void metro_art(']),
 ('ui/blades_hub.inc.hpp',['void draw_blades_hub() {']),
 ('ui/kinect_hub.inc.hpp',['void draw_kinect_hub() {']),
 ('ui/metro_hub.inc.hpp',['void draw_metro_hub() {']),
 ('ui/guide.inc.hpp',['namespace guide {']),
 ('ui/dashboard_picker.inc.hpp',['void draw_dashboard_picker() {']),
 ('assets/update_art.inc.hpp',['void refresh_update_art() {']),
 ('runtime/avatar_sprites.inc.hpp',['namespace avatar {']),
 ('runtime/avatar_scene.inc.hpp',['namespace look3d {']),
 ('runtime/sound_effects.inc.hpp',['namespace sfx {']),
 ('runtime/music_player.inc.hpp',['namespace tunes {']),
 ('runtime/trailer_player.inc.hpp',['void load_trailers() {','void update_trailer(']),
 ('ui/nxe_cards.inc.hpp',['void draw_cards() {']),
 ('ui/hub_dispatch.inc.hpp',['void draw_hub(Uint32 now) {']),
 ('ui/guide_profile.inc.hpp',['std::string clock_text() {','void draw_gamercard() {']),
 ('ui/controller_and_details.inc.hpp',['void draw_controller_map(','void draw_info() {']),
 ('ui/fullscreen_media.inc.hpp',['void draw_viewer_full() {']),
 ('ui/media_pages.inc.hpp',['void draw_avatar_stage() {','void draw_avatar_editor() {']),
 ('ui/library.inc.hpp',['void draw_library_static() {','void draw_library() {']),
 ('ui/viewer_dispatch.inc.hpp',['void draw_viewer() {']),
 ('ui/notifications.inc.hpp',['void draw_toast(']),
]
for target,signatures in groups:
    if '#include "'+target+'"' in source:continue
    ranges=[block(source,s) for s in signatures]
    start,end=ranges[0][0],ranges[-1][1]
    implementation=source[start:end]
    dest=ROOT/target;dest.parent.mkdir(exist_ok=True)
    if dest.exists():raise ValueError('Refuse to overwrite '+target)
    header='// Included inside main.cpp\'s private namespace; see docs/CODE_MAP.md.\n'
    dest.write_text(header+implementation+'\n')
    directive='#include "'+target+'"'
    source=source[:start]+directive+source[end:]
    records.append({'file':target,'implementation_sha256':hashlib.sha256(implementation.encode()).hexdigest(),
                    'lines':implementation.count('\n')+1})
reconstructed=source
for item in records:
    content=(ROOT/item['file']).read_text().split('\n',1)[1].removesuffix('\n')
    reconstructed=reconstructed.replace('#include "'+item['file']+'"',content,1)
if reconstructed!=original:raise ValueError('Split changed source logic')
path.write_text(source)
report=ROOT/'docs/source-split.json'
previous=json.loads(report.read_text()).get('modules',[]) if report.exists() else []
report.write_text(json.dumps({'lossless_reconstruction':True,'modules':previous+records},indent=2)+'\n')
print('Lossless split:',len(records),'modules;',sum(r['lines'] for r in records),'lines grouped by responsibility')
