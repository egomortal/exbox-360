"""Build private NXUI packs from verified dashboard scenes and skin subtrees."""
import argparse
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

from custom_scenes import decode, members, walk
from toxml import node as xml_node
from xur8 import for_xml
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compile_xui_scene import Compiler, FIELDS


class SceneCompiler(Compiler):
    pass


# Reuse the existing compiler verbatim, selecting XuiScene instead of TabScene.
# In particular, retain its rejection of unsupported easing and key flags.
source = (Path(__file__).resolve().parents[1]/'compile_xui_scene.py').read_text()
body = source[source.index('    def compile(self,main):'):source.index('    def write(self,path):')]
body = body.replace("scene=root.find('XuiTabScene')", "scene=root.find('XuiScene')")
namespace = {}
exec('class _Scene:\n'+body, {'ET': ET, 'FIELDS': FIELDS}, namespace)
SceneCompiler.compile = namespace['_Scene'].compile

BUILDS = {6770: 'Blades/Retail/6770', 9199: 'NXE/Retail/9199',
          13604: 'Kinect/Retail/13604', 15574: 'Metro/V1/Retail/15574',
          17559: 'Metro/V2/Retail/17559'}


def xml(tree, decoder):
    tree = copy.deepcopy(tree)
    # v5 calls the derived XAudio file field XFile to distinguish it from the
    # opaque base Sound0 field. The native File channel is this exact filename
    # event, not Sound0. Preserve all keys; unresolved sound-state tracks still
    # remain unhandled and block compilation rather than being dropped.
    for n in walk(tree):
        if n['class']=='XuiSoundXAudio' and 'XFile' in n.get('props', {}):
            n['props']['File']=n['props'].pop('XFile')
        children={c.get('props', {}).get('Id'):c for c in n.get('children', [])}
        for track in n.get('timelines', {}).get('tracks', []):
            target=children.get(track['target'])
            if target and target['class']=='XuiSoundXAudio':
                track['props']=['File' if name=='XFile' else name for name in track['props']]
    for n in walk(tree):
        for track in n.get('timelines', {}).get('tracks', []):
            if any(flag not in (0, 1, 2) for flag, _ in track.get('flags', [])):
                raise ValueError('Unsupported key flags: '+track['target'])
    return xml_node(for_xml(tree) if decoder.d[7] in (7, 8) else tree, decoder)


def loading_loop(visual):
    """Export only Metro's explicitly named Loop, after its unsupported intro.

    Every retained track must have an exact key at both endpoints. No curve
    sampling, guessed easing or extrapolated starting value is permitted.
    """
    visual = copy.deepcopy(visual)
    for node in walk(visual):
        tl = node.get('timelines')
        if not tl:
            continue
        named = {n[0]: n for n in tl['named']}
        first, last = named['Loop'][1], named['EndNormal'][1]
        assert named['EndNormal'][2:] == [3, 'Loop']
        for track in tl['tracks']:
            keys = track['keys']
            flags = track.get('flags', [[k[1], None] for k in keys])
            selected = [(k, f) for k, f in zip(keys, flags) if first <= k[0] <= last]
            assert selected and selected[0][0][0] == first and selected[-1][0][0] == last
            if any(f[0] not in (0, 1, 2) for _, f in selected):
                raise ValueError('Unsupported curve inside loading Loop: '+track['target'])
            track['keys'] = [k for k, _ in selected]
            track.pop('flags', None)
        tl['named'] = [named['Loop'], named['EndNormal']]
    return visual


def build(root, output, guide_root=None):
    if 'private' not in output.resolve().parts:
        raise ValueError('Compiled scenes must stay under private/')
    guide_root = guide_root or root.parents[2]/'Xbox 360 Guide Recovery/guide-unpacked'
    manifest = []
    for build_id, relative in BUILDS.items():
        directory = output/str(build_id)/'custom-scenes'
        directory.mkdir(parents=True, exist_ok=True)
        packages = {name: dict(members((root/relative/'dash.xex'/name).read_bytes()))
                    for name in ('controlp', 'music', 'dashuisk', 'slots')
                    if (root/relative/'dash.xex'/name).is_file()}
        evidence = []
        skin_data = packages['dashuisk']['skin.xur']
        skin, skin_decoder = decode(skin_data)
        skin_visuals = {n['props']['Id']: n for n in walk(skin)
                        if n['class'] == 'XuiVisual' and 'Id' in n.get('props', {})}

        def get_visual(identity):
            node = skin_visuals[identity]
            return node, skin_decoder, *skin_decoder.ranges[id(node)]

        def visual_dependencies(tree):
            found = {}
            def visit(node):
                for n in walk(node):
                    identity = n.get('props', {}).get('Visual')
                    if identity in skin_visuals and identity not in found:
                        found[identity] = (skin_visuals[identity], skin_decoder)
                        visit(skin_visuals[identity])
            visit(tree)
            return list(found.values())

        def shared_texture(uri):
            name = uri.split('://', 1)[-1]
            candidates = [guide_root/f'2.0.{build_id}.0/u'/module/'shrdres'/name
                          for module in ('xam', 'huduiskin')]
            origin = next((p for p in candidates if p.is_file()), None)
            if origin is None:
                raise ValueError('Missing same-build shared texture: '+uri)
            data = origin.read_bytes()
            (directory/name).write_bytes(data)
            entry = dict(origin=str(origin), member=uri, file=name,
                         sha256=hashlib.sha256(data).hexdigest())
            if entry not in evidence:
                evidence.append(entry)
            return name


        def save_source(module, name):
            data = packages[module][name]
            dest = directory/(module+'-'+name)
            dest.write_bytes(data)
            entry = dict(package=relative+'/dash.xex/'+module, member=name,
                         sha256=hashlib.sha256(data).hexdigest(), file=dest.name)
            if entry not in evidence:
                evidence.append(entry)
            return dest

        def compile_tree(tree, decoder, module, name, stem, visuals=()):
            src = save_source(module, name)
            tree = copy.deepcopy(tree)
            # Resolve only verified same-build shared resources. Runtime uses
            # these local filenames; the original URI remains in source XUR.
            for n in walk(tree):
                image = n.get('props', {}).get('ImagePath', '')
                if image.startswith('sharedres://'):
                    n['props']['ImagePath'] = shared_texture(image)
            main = xml(tree, decoder)
            if main.tag == 'XuiCanvas':
                # Source scene retains its original children, geometry and keys.
                scene = next(c for c in main if c.tag.startswith('Xui'))
            else:
                scene = main
            scene.tag = 'XuiScene'
            canvas = ET.Element('XuiCanvas')
            canvas.append(scene)
            main_path = directory/(stem+'.xui')
            ET.ElementTree(canvas).write(main_path, encoding='utf-8')
            sources = [(main_path, src)]
            if visuals:
                skin = ET.Element('XuiCanvas')
                for visual, vd in visuals:
                    skin.append(xml(visual, vd))
                skin_path = directory/('slot-visuals.xui' if build_id == 9199 else stem+'-visuals.xui')
                ET.ElementTree(skin).write(skin_path, encoding='utf-8')
                sources.append((skin_path, save_source('dashuisk', 'skin.xur')))
            compiler = SceneCompiler(sources).compile(main_path)
            # Unbound default control visuals have no paint; explicit visuals
            # must have been recovered. Never drop an unknown timeline field.
            if compiler.unhandled:
                raise ValueError('Unhandled tracks: '+repr(sorted(compiler.unhandled)))
            with contextlib.redirect_stdout(io.StringIO()):
                compiler.write(directory/(stem+'.nxui'))
            result = dict(pack=stem+'.nxui', nodes=len(compiler.nodes), keys=len(compiler.keys),
                          missing_visuals=sorted(compiler.missing))
            if any(n['class'] == 'XuiVideo' for n in walk(tree)):
                result['runtime_blocker'] = ('Static movie frame has no authored animation tracks; '
                                             'its RGB/alpha ColorWriteFlags 7/8 are not supported by the painter')
            return result

        results = {}
        if 'LegendScene.xur' in packages.get('controlp', {}):
            data = packages['controlp']['LegendScene.xur']
            tree, d = decode(data)
            # The host lays out labels. Recover only the four Press owners;
            # unrelated v8 text colour tracks use unsupported raw key flags.
            buttons = []
            for button in walk(tree):
                identity = button.get('props', {}).get('Id')
                if identity not in ('AButton', 'BButton', 'XButton', 'YButton'):
                    continue
                images = next(copy.deepcopy(c) for c in button['children'] if c.get('props', {}).get('Id') == 'Images')
                images['props']['Id'] = identity
                buttons.append(images)
            assert len(buttons) == 4
            tree = dict({'class': 'XuiScene'}, props=dict(Id='LegendPress', Width=40., Height=40.), children=buttons)
            results['legend'] = compile_tree(tree, d, 'controlp', 'LegendScene.xur', 'legend')
            for name in ('Legend_Highlight.png', 'Legend_A.png', 'Legend_B.png', 'Legend_X.png', 'Legend_Y.png'):
                if name in packages['controlp']:
                    (directory/name).write_bytes(packages['controlp'][name])
                    evidence.append(dict(package=relative+'/dash.xex/controlp', member=name, file=name,
                                         sha256=hashlib.sha256(packages['controlp'][name]).hexdigest()))
        try:
            visual, vd, start, end = get_visual('equalizer_small')
            # Equalizer.xur supplies the visual binding; skin supplies geometry.
            wrapper, _ = decode(packages['music']['Equalizer.xur'])
            assert any(n.get('props', {}).get('Visual') == 'equalizer_small' for n in walk(wrapper))
            save_source('music', 'Equalizer.xur')
            results['equalizer'] = compile_tree(visual, vd, 'dashuisk', 'skin.xur', 'equalizer')
            results['equalizer']['isolated_range'] = [start, end]
        except (AssertionError, KeyError, IndexError, ValueError) as error:
            results['equalizer'] = dict(blocker=str(error))
        try:
            loading, ld, start, end = get_visual('Loading_Large')
            loop_only = build_id in (15574, 17559)
            if loop_only:
                loading = loading_loop(loading)
            results['loading'] = compile_tree(loading, ld, 'dashuisk', 'skin.xur', 'loading')
            results['loading'].update(source_range=[start, end], loop_only=loop_only,
                                      width=loading['props']['Width'], height=loading['props']['Height'])
        except (AssertionError, KeyError, IndexError, ValueError) as error:
            results['loading'] = dict(blocker=str(error))
        if build_id >= 9199:
            visuals = [get_visual(identity)[:2] for identity in ('defaultSlotAnimation1', 'defaultSlotAnimation2')] if build_id == 9199 else []
            for name, data in packages['slots'].items():
                if not name.endswith('SlotScene.xur') or name in ('GamerCardSlotScene.xur', 'FamilyCenterSlotScene.xur'):
                    continue
                try:
                    tree, d = decode(data)
                    results[name] = compile_tree(tree, d, 'slots', name, name[:-4], visuals if build_id == 9199 else visual_dependencies(tree))
                except (AssertionError, KeyError, IndexError, ValueError) as error:
                    results[name] = dict(blocker=str(error))
        manifest.append(dict(build=build_id, sources=evidence, results=results,
                             skin_validation=dict(data_end=skin_decoder.at, data_start=skin_decoder.parts['DATA'][0],
                                                  data_bytes=skin_decoder.parts['DATA'][1],
                                                  counts=getattr(skin_decoder, 'got', None))))
        (directory/'sources.json').write_text(json.dumps(manifest[-1], indent=2)+'\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path, default=Path('private/era-skins'))
    parser.add_argument('--guide-root', type=Path, help='Read-only guide-unpacked directory for same-build shared textures')
    args = parser.parse_args()
    for row in build(args.root, args.output, args.guide_root):
        print(row['build'], {k: v.get('blocker', v.get('pack')) for k, v in row['results'].items()})
