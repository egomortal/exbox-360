"""Build the native editor's listing-free index without changing asset bytes.

Records sort by parent UTF-8 bytes, then basename UTF-8 bytes. Encoding is shared
by standalone filesystem inventory and setup's already verified planned entries.
The guest-facing AV2IDX01 format and metadata meanings remain unchanged.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,stat
HEADER=struct.Struct('<8sIIQ')
RECORD=struct.Struct('<1024sQB7x')
MAGIC=b'AV2IDX01'


def rows_from_entries(entries):
    rows=[];folds={}
    for rel,size,kind in entries:
        if rel in ('.av2-index','.av2-index.tmp'):continue
        raw=rel.encode('utf-8');name=rel.rpartition('/')[2]
        if kind not in (1,2) or size<0 or len(raw)>=1024 or len(name.encode('utf-8'))>=256:
            raise ValueError('unsupported entry: '+rel)
        if any(c in rel for c in ('\0','\\',':')) or any(x in ('','.','..') for x in rel.split('/')):
            raise ValueError('unsafe path: '+rel)
        fold=rel.casefold()
        if fold in folds:raise ValueError('ambiguous case-fold: '+folds[fold]+' / '+rel)
        folds[fold]=rel;parent,_,leaf=rel.rpartition('/')
        rows.append((parent.encode('utf-8'),leaf.encode('utf-8'),raw,size if kind==1 else 0,kind))
    if len(rows)>1000000:raise ValueError('asset index exceeds runtime limit')
    return sorted(rows)


def inventory(root):
    root=Path(root).resolve(strict=True);entries=[]
    def walk_error(error):raise error
    for base,dirs,files in os.walk(root,followlinks=False,onerror=walk_error):
        for name in dirs+files:
            path=Path(base)/name;rel=path.relative_to(root).as_posix()
            if rel in ('.av2-index','.av2-index.tmp'):continue
            st=path.lstat()
            if stat.S_ISLNK(st.st_mode):raise ValueError('symlink in asset tree: '+rel)
            kind=2 if stat.S_ISDIR(st.st_mode) else 1 if stat.S_ISREG(st.st_mode) else 0
            entries.append((rel,st.st_size if kind==1 else 0,kind))
    return rows_from_entries(entries)


def encode(rows):
    return HEADER.pack(MAGIC,RECORD.size,len(rows),0)+b''.join(RECORD.pack(raw,size,kind) for _,_,raw,size,kind in rows)


def build(root,output):
    root=Path(root).resolve(strict=True);output=Path(output)
    if output.parent.resolve()!=root or output.name!='.av2-index':
        raise ValueError('output must be asset-root/.av2-index')
    rows=inventory(root);data=encode(rows)
    temp=output.with_name('.av2-index.tmp');temp.write_bytes(data);os.replace(temp,output)
    return dict(root=str(root),path=str(output),entries=len(rows),files=sum(row[4]==1 for row in rows),
                directories=sum(row[4]==2 for row in rows),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
                format='AV2IDX01',kind='derived filesystem metadata; original assets unchanged')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);parser.add_argument('--report',type=Path);args=parser.parse_args()
    report=build(args.root,args.root/'.av2-index')
    if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
