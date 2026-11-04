"""Inventory a proposed source-only export without publishing or moving files."""
from pathlib import Path
import argparse,hashlib,json,os,re

PROJECT=Path(__file__).resolve().parents[1]
SKIP={'.git','.codex','.agents','.aws','.claude','private','inputs','build','backups','outputs','out','node_modules','__pycache__','CMakeFiles'}
TEXT={'.py','.cpp','.hpp','.h','.c','.cc','.cmake','.sh','.cmd','.ps1','.md','.json','.toml','.ini','.yml','.yaml','.txt','.map','.ld','.inc'}
SPECIAL={'Makefile','CMakeLists.txt','LICENSE','COPYING','.gitignore','.gitattributes','.editorconfig'}
BINARY={'.png','.jpg','.jpeg','.bmp','.dds','.svg','.wav','.xma','.mp3','.wma','.ogg','.flac','.wmv','.mp4','.mkv','.zip','.7z','.tgz','.gz','.xex','.xexp','.xzp','.xur','.xus','.nxui','.nxak','.elf','.exe','.dll','.so','.a','.o','.obj','.bin','.pack','.ttf','.otf','.xtt'}


def audit(root=PROJECT):
    root=Path(root).resolve(strict=True);candidates=[];excluded=[];review=[];skipped=[]
    for directory,dirs,files in os.walk(root,followlinks=False):
        base=Path(directory)
        keep=[]
        for name in sorted(dirs):
            path=base/name;relative=path.relative_to(root).as_posix()
            if name in SKIP or path.is_symlink():skipped.append(dict(path=relative,reason='local/private/build history or symlink; not exported'))
            else:keep.append(name)
        dirs[:]=keep
        for name in sorted(files):
            path=base/name;relative=path.relative_to(root).as_posix()
            if path.is_symlink():excluded.append(dict(path=relative,reason='symlink'));continue
            if name=='.env' or name.startswith('.env.') or path.suffix.lower() in BINARY:
                excluded.append(dict(path=relative,bytes=path.stat().st_size,reason='binary/media/original input or local configuration; not source-only'))
                continue
            if path.suffix.lower() not in TEXT and name not in SPECIAL:
                review.append(dict(path=relative,bytes=path.stat().st_size,reason='unclassified export type'));continue
            data=path.read_bytes();record=dict(path=relative,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
            try:text=data.decode('utf-8')
            except UnicodeError:
                review.append(dict(record,reason='source candidate encoding needs review; bytes preserved'));continue
            if b'\0' in data or data[:4] in [b'XEX2',b'XUIZ',b'\x7fELF',b'PK\x03\x04']:
                review.append(dict(record,reason='embedded binary/signature in source candidate'));continue
            if len(re.findall(r'0x[0-9a-fA-F]{2}\s*,',text))>2048 or len(re.findall(r'data:image/[a-zA-Z0-9.+-]+;base64,[A-Za-z0-9+/=]{128,}',text)):
                review.append(dict(record,reason='possible embedded asset array/data URI; provenance review required'));continue
            record['spdx']=re.findall(r'SPDX-License-Identifier:\s*([^\r\n]+)',text)[:4]
            candidates.append(record)
    external=[]
    make=root/'Makefile'
    if make.is_file():
        for value in sorted(set(re.findall(r'\.\./(?:native-overlay|native-app)/[^\s\\]+',make.read_text(encoding='utf-8')))):
            path=(root/value).resolve();external.append(dict(reference=value,exists=path.exists(),reason='standalone export needs this sibling source/interface'))
    license_present=any((root/name).is_file() for name in ['LICENSE','COPYING','LICENSE.md','LICENSE.txt'])
    return dict(schema=1,status='audit-only; human export review still required',source_root=str(root),
                published=False,git_initialized=False,source_candidates=candidates,excluded_data=excluded,
                review_required=review,skipped_roots=skipped,external_dependencies=external,root_license_present=license_present,
                limitations=['File signatures/extensions cannot prove asset ownership or detect every embedded payload.','No files were copied to a public repository or deleted.','Existing third-party notices must accompany any later authorized source export.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',type=Path,default=PROJECT);parser.add_argument('--output',type=Path,default=PROJECT/'private/source-export-audit.json');args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to((PROJECT/'private').resolve()):parser.error('Audit output must stay under project/private')
    result=audit(args.root);output.parent.mkdir(parents=True,exist_ok=True)
    if output.exists():raise ValueError('Use a fresh audit output; previous result preserved')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=result['status'],source_candidates=len(result['source_candidates']),excluded_data=len(result['excluded_data']),review_required=len(result['review_required']),external_dependencies=result['external_dependencies'],root_license_present=result['root_license_present'],output=str(output)),indent=2))


if __name__=='__main__':main()
