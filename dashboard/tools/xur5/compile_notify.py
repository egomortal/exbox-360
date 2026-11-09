"""Compose notify.xur and its Guide skin; keep all recovered content private.

Usage: python tools/xur5/compile_notify.py OUT --source BUILD/u
GUIDE_SOURCE may supply --source (1888 uses unpacked-corrected).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

from guide_pack import Pack, ET, write_xml


def compile_notify(source, out):
    source, out = Path(source), Path(out)
    if 'private' not in out.resolve().parts:
        raise ValueError('Notification packs and decoded scenes must stay under private/')
    def locate(choices):
        return next((rel for rel in choices if (source/rel).is_file()), None)
    notify = locate(('xam/xam/notify.xur', 'xam/notify.xur'))
    skin = locate(('xam/skin/skin.xur', 'huduiskin/skin/skin.xur', 'skin/skin.xur'))
    if not notify or not skin:
        raise FileNotFoundError('notify.xur or skin.xur missing in '+str(source))
    p = Pack(source, out.parent/(out.stem+'-xml'))
    sk = p.load(skin, 'skin.xui')
    main = p.load(notify, 'notify.xui')
    control = p.find(main, 'PopupControl')
    if control is None:
        raise ValueError('notify.xur has no PopupControl')
    # 9199's decoder maps the sample text to ImagePath; host binds both slots.
    for name in ('Text', 'ImagePath'):
        prop = control.find('Properties/'+name)
        if prop is not None:
            control.find('Properties').remove(prop)
    # The caller already plays T01's era sound. Never compile a second sound.
    for parent in sk.iter():
        sounds = {e.findtext('Properties/Id') for e in parent if e.tag.startswith('XuiSound')}
        for e in list(parent):
            if e.tag.startswith('XuiSound'):
                parent.remove(e)
        timelines = parent.find('Timelines')
        if timelines is not None:
            for t in list(timelines):
                if t.tag == 'Timeline' and t.findtext('Id') in sounds:
                    timelines.remove(t)
    write_xml(sk, p.tmp/'skin.xui')
    c = p.compile(main, out)
    if c.missing or c.unhandled:
        raise ValueError('Incomplete notification compilation: '+str((c.missing, c.unhandled)))
    def token(offset):
        return c.strings[offset:c.strings.index(0, offset)].decode('utf-8')
    textures = {token(n['values'][22][1]) for n in c.nodes if n['values'][22][1]}
    textures.update(token(k[5][1]) for t in c.tracks if t[2] == 22
                    for k in c.keys[t[4]:t[4]+t[5]] if k[5][1])
    files = []
    # Prefer the matching skin directory for bare filenames and XAM for xam://.
    for key in sorted(textures | {'Achievement.png', 'secretAchievement.png', 'unearnedAchievement.png'}):
        name = key.split('://')[-1]
        if Path(name).name != name or not name.lower().endswith('.png'):
            raise ValueError('Unsupported notification texture '+key)
        choices = [str(Path(skin).parent/name), 'xam/xam/'+name, 'huduiskin/xam/'+name,
                   'xam/shrdres/'+name, 'xam/'+name, 'shrdres/'+name]
        if key.startswith('xam://'):
            choices.insert(0, 'xam/xam/'+name)
        rel = locate(choices)
        if rel is None:
            if key in textures:
                raise FileNotFoundError(key)
            continue  # 1888 has no Achievement.png: runtime uses caller's trophy.
        shutil.copyfile(source/rel, out.parent/name)
        files.append(dict(file=name, source=str(source/rel),
                          sha256=hashlib.sha256((out.parent/name).read_bytes()).hexdigest()))
    manifest = dict(source=str(source), images=files,
                    missing_optional_icons=sorted({'Achievement.png', 'secretAchievement.png', 'unearnedAchievement.png'}
                                                  - {f['file'] for f in files}),
                    slots={n['id']: i for i, n in enumerate(c.nodes)
                           if n['id'] in ('PopupControl', 'TextPresenter', 'Image', '@visual:scr_Notification')},
                    frames=[dict(name=token(r[0]), frame=r[1], command=r[2])
                            for a in c.animations if c.nodes[a[0]]['id'] == '@visual:scr_Notification'
                            for r in c.ranges[a[1]:a[1]+a[2]]])
    out.with_suffix('.assets.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source', default=os.environ.get('GUIDE_SOURCE'))
    args = parser.parse_args()
    if not args.source:
        parser.error('--source or GUIDE_SOURCE is required')
    compile_notify(args.source, args.output)
