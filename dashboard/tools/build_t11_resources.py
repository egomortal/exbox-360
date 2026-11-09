"""Stage T11's private text and referenced art from locally extracted owner modules.

Run tools/t11-resources.cjs first. No network, dependencies, or resource-folder writes.
"""
from pathlib import Path
import argparse,html,json,re,shutil,struct,subprocess

ROOT=Path(__file__).resolve().parent.parent
SOURCE=ROOT/'private/astra-t11'
manifest=[]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ffmpeg',default=shutil.which('ffmpeg'))
args=parser.parse_args()
def strings(p):
    d=p.read_bytes();i=d.index(b'STRN');o,n=struct.unpack_from('>II',d,i+4)
    if struct.unpack_from('>I',d,4)[0]==8:
        return d[o+6:o+n].decode().rstrip('\0').split('\0')
    out=[];at=o
    while at<o+n:
        length=struct.unpack_from('>H',d,at)[0];at+=2
        out.append(d[at:at+length*2].decode('utf-16be'));at+=length*2
    return out
def plain(s):
    s=re.sub(r'\s+',' ',s)
    s=re.sub(r'<br\s*/?>|</p>|</li>', '\n',s,flags=re.I)
    s=re.sub(r'<li[^>]*>', '• ',s,flags=re.I)
    return html.unescape(re.sub('<[^>]+>','',s)).strip()
def scene(build,module,name):
    return strings(SOURCE/str(build)/module/(name+'.xur'))
def find_text(s,prefix):
    return next((x for x in s if x.startswith(prefix)), '')
def heading(s):
    for marker in ['XuiText1','Label_Head','Label_OOBE_Header']:
        if marker in s:
            tail=s[s.index(marker)+1:]
            for x in tail:
                if not re.match(r'^(Xui|Label_|Text|<|lab|common:|.*\.(jpg|png)$)',x) and x.strip():return x.strip()
    return next((x for x in s if x in ['New Avatar Items','Join Xbox LIVE']), '')

for build in [1888,6770,9199,13604,15574,17559]:
    src=SOURCE/str(build);out=ROOT/f'private/era-skins/{build}/welcome';out.mkdir(parents=True,exist_ok=True)
    rows=[]
    def art(ref,module='firstrun',source_build=build):
        if not ref:return ''
        name=ref.split('://')[-1];mod='dashcomm' if ref.startswith('common://') else module
        parent=SOURCE/str(source_build)/mod
        p=next((x for x in parent.glob('*') if x.name.lower()==name.lower()),None)
        if p is None:return ''
        dest=f'{mod}-{p.name}';shutil.copyfile(p,out/dest)
        manifest.append({'file':str((out/dest).relative_to(ROOT)),'source':str(p.relative_to(ROOT))})
        return dest
    def row(key,title,body='',image=''):
        rows.append('\t'.join([key,title.replace('\n',' ').strip(),image,body.replace('\t',' ').replace('\r','').replace('\n','\\n')]))
    firstrun=src/'firstrun'
    if build in [9199,13604]:
        for p in sorted(firstrun.glob('*Scene.xur')):
            if not re.match(r'(XboxBasics|WhatsNew)',p.stem) or re.search('Root|Slot',p.stem):continue
            s=strings(p);body=next((x for x in s if '<font' in x),'')
            if not body:continue
            image=next((x for x in s if re.search(r'\.(jpg|png)$',x,re.I) and 'CornerMask' not in x),'')
            row(p.stem,heading(s),plain(body),art(image))
        slots=['WhatsNewSlotScene','XboxBasicsSlotScene','NxeVideoSlotScene','HideWelcomeSlotScene'] if build==9199 else ['XboxBasicsSlotScene','XboxKinectSlotScene','XboxGoldFamilyPackSlotScene','DashFirstUseTutorialSlotScene','WirelessWelcomeSlotScene','HideWelcomeSlotScene']
        for name in slots:
            s=scene(build,'firstrun',name)
            # Slot strings contain structural identifiers; choose the sentence and image by role.
            title=next((x.strip() for x in s if x.strip() in ["What's Hot",'Xbox Essentials','Xbox LIVE Video','Hide Channel','Getting Started','Kinect for Xbox 360','Family First','Xbox 360 Tutorials','Go Wireless']),'')
            body=next((x.strip() for x in s if (' ' in x and x.strip().endswith(('.', '!')))), '')
            image=next((x for x in s if re.search(r'\.(jpg|png)$',x,re.I) and not re.search(r'Epix|slot_panel',x)), '')
            row(name,title,body,art(image))
        # XUS differs between v5 (UTF-16BE) and v8 (UTF-8). Preserve each build's prompt.
        d=(src/'dashcomm/dashStrings.xus').read_bytes()
        variants=[d.decode('utf-8',errors='ignore'),d[:len(d)//2*2].decode('utf-16be',errors='ignore')]
        prompt=''
        for text in variants:
            match=re.search(r'Do you want to remove this channel\?.*?(?:Welcome Channel\.|Startup and Shutdown\.)',text)
            if match:prompt=match[0];break
        assert prompt,(build,'hide prompt')
        row('hide','Hide Channel',prompt,art('HideWelcomeSlot.jpg'))
        row('background','',image=art('EpixGreen.jpg'));art('EpixOrange.jpg')
    if build in [15574,17559]:
        icons=['ico_64x_console.png','ico_64x_SettingsPreferences.png','ico_64x_pic.png','ico_64x_kinect_device.png','ico_64x_accountmanagement.png','ico_64x_privacy.png','ico_64x_family.png','ico_64x_TurnOff.png','xboxBasics.jpg','xboxNetwork.jpg']
        for icon in icons:
            row('tile/'+icon,'',image=art('common://'+icon))
    # The five useful local steps reuse Console Settings where the retail OOBE
    # has no corresponding step. These adaptations are documented in the report.
    if build in [13604,15574]:
        intro=scene(build,'oobe','oobeIntroXbox');done=scene(build,'oobe','oobeCompleteXbox')
        row('setup/0','Console Settings',find_text(intro,"Let's start")+'\n\n'+find_text(intro,'Display settings')+'\n'+find_text(intro,'Auto-off'),art('common://xboxLogotype_twoThirdsPane.png'))
        row('setup/4','Console Settings',find_text(done,'Console settings are done'),art('common://xboxLogotype_twoThirdsPane.png'))
        art('Controller1_DFU.png','oobe');art('globe.png','oobe')
    elif build==17559:
        intro=scene(build,'oobe','oobeControllerNoLanguage');done=scene(build,'oobe','oobeCompleteLive')
        row('setup/0',find_text(intro,'Welcome.'),'',art('Controller1_DFU.png','oobe'))
        row('setup/4',find_text(done,"You're ready"),find_text(done,'Thanks for choosing'),'')
    elif build==9199:
        done=scene(build,'noobe','noobeComplete')
        row('setup/0','Initial Setup','',art('Oobe_Xbox360Live.png','noobe'))
        row('setup/4','Setup Complete',plain(find_text(done,"You're now ready")),art('common://oobe.png'))
    else:
        done=scene(build,'oobe','oobeWelcome') if (src/'oobe/oobeWelcome.xur').exists() else scene(6770,'oobe','oobeWelcome')
        row('setup/0','Initial Setup','',art('common://xboxLogo.png'))
        row('setup/4','Initial Setup Complete',find_text(done,'You can now play') or find_text(done,'Congratulations!'),art('common://xboxLogo.png'))
    logo=art('common://xboxLogo.png') or art('common://oobe.png') or art('Controller1_DFU.png','oobe')
    row('setup/1','Display Settings','',art('display96x120.png','oobe') or logo)
    # Ex-Box addition: no 360 original for this explanation of PS5 idle dimming.
    auto_text=find_text(scene(build,'oobe','oobeAutoOff'),'This Xbox') if build in [13604,15574] else ''
    row('setup/2','Auto-Off',(auto_text+'\n\n' if auto_text else '')+'On PS5, this setting dims the screen after ten idle minutes.',art('common://ico_64x_console.png') or art('common://console_64x.png') or logo)
    row('setup/3','Time Format','',art('common://xboxLogotype_twoThirdsPane.png') or logo)
    (out/'pages.tsv').write_text('\n'.join(rows)+'\n',encoding='utf-8')
    manifest.append({'file':str((out/'pages.tsv').relative_to(ROOT)),'source':str(src.relative_to(ROOT)),'rows':len(rows)})

missing_audio=[]
for build in [13604,15574]:
    source=SOURCE/str(build)/'oobe/celebrate_sound.xma'
    target=ROOT/f'private/ps5/sounds/eras/{build}/dash/celebrate_sound.wav'
    if args.ffmpeg:
        target.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run([args.ffmpeg,'-y','-i',str(source),'-ac','2','-ar','48000','-c:a','pcm_s16le',str(target)],check=True)
        manifest.append({'file':str(target.relative_to(ROOT)),'source':str(source.relative_to(ROOT))})
    elif not target.exists():
        missing_audio.append({'source':str(source.relative_to(ROOT)),'target':str(target.relative_to(ROOT))})
(SOURCE/'missing-audio.json').write_text(json.dumps(missing_audio,indent=2),encoding='utf-8')
(SOURCE/'staged-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(f'Staged {len(manifest)} asset/text records; provenance in private/astra-t11/staged-manifest.json')
if missing_audio:print('ffmpeg unavailable: completion WAVs remain to be converted (private/astra-t11/missing-audio.json).')
