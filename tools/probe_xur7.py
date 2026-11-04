from pathlib import Path
import json,traceback
from decode_xur7 import decode
from scene_catalog import inventory_path,decoder_settings
assets=Path('C:/Users/USER/Downloads/jailbreak stuff/asset/exbox360');inv=json.loads(inventory_path(assets).read_text())
source=next(assets/'360dasharchive'/s['path'] for u in inv['updates'] for v in u['variants'] for s in v['scenes'] if u['build']==11025 and 'AuraScene.xur' in s['path'])
try:print(decode(source,assets/decoder_settings(assets)['schema'],assets/'scene-audit/revisions/v7-decoded-probe.xui'))
except Exception:traceback.print_exc(limit=-3)
