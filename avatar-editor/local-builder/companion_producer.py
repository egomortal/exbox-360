"""Original local companion generator. Requires externally supplied assets.

No network, console access or embedded artwork. SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import hashlib,json,struct,zlib
import subprocess,tempfile

XTT='977bbe13d5545d300e57dacf9dff492476452f50880ef3868d3aa308d041187d'
LIBC_INPUTS={
 'tooling/native/libc_builder.cpp':'fb9e4eb66ef6c1d9e9bcfd059a3a3aaf86156fe065838303a0c3fbd789786a1a',
 'tooling/native/runtime/api-surface.txt':'d9a81aec01f077817c35f3225f8325126bd63fd077a57734454a1868f6b25052',
 'tooling/native/runtime/imports.txt':'ef8f04e17c355495ee8120ef28b1648f7d4380eb5b9baeb7ab50034ffacfe8ea',
}
RAW_LIBC='8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb'

INPUTS={
 'Legend_Y.png':'a1403c004bd70a517c7df65715e9bdbfe15069d1664fbaaf562301f160ede21e',
 'dialog_bg.png':'39019f76a8316e7f027ca444d9015f10b9c8b231fc6f74689cbaf5dd60c0558f',
 'option_dialog_yes_selected.png':'a5614eee303a15e427d4d480fcbe69db33e232c736fc819ab32b1851246bb2a3',
 'option_dialog_yes_unselected.png':'0613094ff9bd2cd9249938255ad828a8776d16ae4790f81ce2d3222b65814ef9',
 'option_dialog_no_selected.png':'de5e521486daf5e8bce9738afcc059e5a579fbe4a6aa6019b09c7b80f568aca7',
 'option_dialog_no_unselected.png':'2871018fb3e6f8bdf2e410e39f76e5de27f2a47e313c6bfe5069c76590d6a36f',
 'ico_64x_Back.png':'6c0971c87f7345b133f6e6516d33b1aa4efb3aa7da633564f3acb7cc362f082c',
 'Legend_A.png':'964ff07a55a24feb774519d31442784c2ec997a25703133ee2c3e8915f40ea66',
 'Legend_B.png':'efeaf06e71acc54276684b2740e42843c233017f40ac6723f6197a88ce98ea46',
}
FONT='9aa8631de0e0ccaaacf72dcf3a32f2e4f85d106bd1ec042dfe0279442918b0a8'
EXPECTED={
 'assets/ui/sort-filter-legend.rle':'553bc771e1371fc0cd8b5bb3635eb8d9b0b0fd8e2eaf7fedf37d78fbc8647bdd',
 'assets/ui/back-prompt.rle':'f3644bbca0523843edb1fd9662e45dad567f254ff6d2359d3e46c2ad1b771f10',
 'assets/ui/dashboard-legend.rle':'7ac94d37375ae9c059ae77ce0695c565e28d641ea76499d8c45719f5c19851ac',
 'sce_sys/icon0.png':'a16d6dfbfac885c42a0f9bb2d16b89cdc3d7fa1cdbf1c787b383e2744ffd352e',
 'sce_sys/param.json':'7768afbddaeb2762a345df5b6fe1ccf44bc65cc5b98ed03cfb960d52c41aa07d',
 'sce_module/libc.prx':'e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036',
}
def sha(data):return hashlib.sha256(data).hexdigest()
def checked(path,fingerprint):
    data=Path(path).read_bytes()
    if sha(data)!=fingerprint:raise ValueError('Unsupported input: '+Path(path).name)
    return data
def rle(image):
    pixels=list(image.getdata());data=bytearray();i=0
    while i<len(pixels):
        count=1
        while i+count<len(pixels) and count<65535 and pixels[i+count]==pixels[i]:count+=1
        data+=struct.pack('<H4B',count,*pixels[i]);i+=count
    return data
def fnv(data):
    value=2166136261
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value
def generate(assets,font,libc):
    from PIL import Image,ImageDraw,ImageFont
    from io import BytesIO
    images={name:Image.open(BytesIO(checked(Path(assets)/name,pin))).convert('RGBA') for name,pin in INPUTS.items()}
    checked(font,FONT)
    def text(draw,x,y,value,size=28,anchor='mm',color=(235,235,235,255)):
        face=ImageFont.truetype(str(font),size)
        draw.text((x+1,y+2),value,font=face,fill=(15,15,15,128),anchor=anchor)
        draw.text((x,y),value,font=face,fill=color,anchor=anchor)
    frames=[]
    for choice in range(3):
        im=Image.new('RGBA',(960,480));im.alpha_composite(images['dialog_bg.png'].resize((960,480),Image.Resampling.LANCZOS));draw=ImageDraw.Draw(im)
        text(draw,480,70,'Leave Avatar Editor?',40);text(draw,480,120,'Save your avatar or discard your changes.',27)
        for n,(label,x) in enumerate([('Save',235),('Discard',480),('Cancel',725)]):
            if n<2:
                name='option_dialog_'+('yes' if n==0 else 'no')+('_selected.png' if n==choice else '_unselected.png');icon=images[name]
            else:
                icon=Image.new('RGBA',(128,128));icon.alpha_composite(images['dialog_bg.png'].resize((128,128),Image.Resampling.LANCZOS));icon.alpha_composite(images['ico_64x_Back.png'].resize((80,80),Image.Resampling.LANCZOS),(24,24))
            im.alpha_composite(icon,(x-64,164));color=(235,235,235,255)
            if n==choice:draw.line((x-66,345,x+66,345),fill=(166,214,28,255),width=4);color=(255,255,255,255)
            text(draw,x,320,label,32,color=color)
        text(draw,480,375,'D-pad to choose',23)
        for x,name,label in [(328,'Legend_A.png','Select'),(508,'Legend_B.png','Cancel')]:
            im.alpha_composite(images[name].resize((28,28),Image.Resampling.LANCZOS),(x,403));text(draw,x+38,417,label,24,anchor='lm')
        frames.append(im)
    data=bytearray();offsets=[]
    for image in frames:offsets.append(len(data));data+=rle(image)
    offsets.append(len(data))
    prompt=b'A1PROMPT'+struct.pack('<6I',1,960,480,3,len(data),fnv(data))+struct.pack('<4I',*offsets)+data
    im=Image.new('RGBA',(256,40));icon=images['Legend_B.png']
    if icon.size!=(32,32):raise ValueError('Unsupported Legend_B size')
    im.alpha_composite(icon,(0,4));draw=ImageDraw.Draw(im);face=ImageFont.truetype(str(font),20)
    draw.text((37,21),'Dashboard',font=face,fill=(15,15,15,128),anchor='lm');draw.text((36,20),'Dashboard',font=face,fill=(255,255,255,255),anchor='lm')
    data=rle(im);legend=struct.pack('<8s6I',b'A1LEGEND',1,256,40,len(data),fnv(data),0)+data
    im=Image.new('RGBA',(256,40));icon=images['Legend_Y.png']
    if icon.size!=(32,32):raise ValueError('Unsupported Legend_Y size')
    im.alpha_composite(icon,(0,4));draw=ImageDraw.Draw(im);face=ImageFont.truetype(str(font),20)
    draw.text((37,21),'Sort / Filter',font=face,fill=(15,15,15,128),anchor='lm');draw.text((36,20),'Sort / Filter',font=face,fill=(255,255,255,255),anchor='lm')
    data=rle(im);sort_filter_legend=struct.pack('<8s6I',b'A1LEGEND',1,256,40,len(data),fnv(data),0)+data
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    raw=(b'\0'+bytes((32,128,191))*512)*512
    icon=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',512,512,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    meta={'applicationCategoryType':0,'titleId':'PPSA99734','kernel':{'flexibleMemorySize':1073741824},'localizedParameters':{'defaultLanguage':'en-US','en-US':{'titleName':'AV2 Editor (Bring-up)'}},'applicationDrmType':'free','contentVersion':'01.000.000','masterVersion':'01.00','contentId':'UP9000-PPSA99734_00-AV2EDITORTEST0001','requiredSystemSoftwareVersion':'0x0000000000000000','sdkVersion':'0x0000000000000000'}
    result={'assets/ui/sort-filter-legend.rle':sort_filter_legend,'assets/ui/back-prompt.rle':prompt,'assets/ui/dashboard-legend.rle':legend,'sce_sys/icon0.png':icon,'sce_sys/param.json':(json.dumps(meta,indent=2)+'\n').encode(),'sce_module/libc.prx':checked(libc,EXPECTED['sce_module/libc.prx'])}
    for name,data in result.items():
        if sha(data)!=EXPECTED[name]:raise ValueError('Generated companion differs: '+name+' sha256='+sha(data))
    return result

def generate_from_originals(assets,xtt,libc):
    """Render from externally supplied original PNGs and XTT, validating all hashes."""
    from extract_xtt_font import convert
    checked(xtt,XTT)
    with tempfile.TemporaryDirectory(prefix='editor-font-') as temp:
        font=Path(temp)/'Convection.ttf'
        convert(Path(xtt),font)
        return generate(assets,font,libc)

def build_libc(vulkan_source,compiler,self_tool,work):
    """Build the pinned clean-room runtime locally; tools/dependencies already supplied."""
    source=Path(vulkan_source).resolve(strict=True)
    # clang++ may be a symlink to clang: preserve its invocation name/C++ driver mode.
    compiler=Path(compiler).absolute();self_tool=Path(self_tool).absolute()
    if not compiler.is_file() or not self_tool.is_file():raise ValueError('Missing supplied native build tool')
    work=Path(work).resolve()
    if work==source or work.is_relative_to(source) or source.is_relative_to(work):
        raise ValueError('Runtime work folder must be separate from dependency sources')
    if work.exists():raise ValueError('Runtime build requires a fresh work folder')
    for name,pin in LIBC_INPUTS.items():checked(source/name,pin)
    work.mkdir(parents=True)
    executable=work/'libc-builder'
    commands=[([str(compiler),'-std=c++20','-O2','-Wall','-Wextra','-Werror',str(source/'tooling/native/libc_builder.cpp'),'-o',str(executable)],'compile.log')]
    for argv,name in commands:
        with (work/name).open('wb') as log:subprocess.run(argv,cwd=work,stdout=log,stderr=subprocess.STDOUT,check=True)
    results=[]
    for suffix in ('a','b'):
        raw=work/('libc-'+suffix+'.raw.elf');out=work/('libc-'+suffix+'.prx')
        with (work/('generate-'+suffix+'.log')).open('wb') as log:
            subprocess.run([str(executable),str(source/'tooling/native/runtime/api-surface.txt'),str(source/'tooling/native/runtime/imports.txt'),str(raw)],cwd=work,stdout=log,stderr=subprocess.STDOUT,check=True)
            checked(raw,RAW_LIBC)
            subprocess.run([str(self_tool),'self','--sign','--in',str(raw),'--out',str(out)],cwd=work,stdout=log,stderr=subprocess.STDOUT,check=True)
        checked(out,EXPECTED['sce_module/libc.prx']);results.append(out)
    if results[0].read_bytes()!=results[1].read_bytes():raise ValueError('Runtime double-build differs')
    return results[0]
