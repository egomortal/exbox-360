"""Stage previously recovered Guide/scene/promotion/media packs unchanged."""
from pathlib import Path
import os

# Source-archive snapshots, native objects, user saves and the protected older
# avatar renderer are deliberately outside these data preparation profiles.
# Trailers have their own importer; raw frames/backups are not dashboard setup inputs.
SCOPES=('era-skins','custom-scenes','promotions','fonts-owner','ps5','xbox/huduiskin')


def files_under(root):
    pending=[root]
    while pending:
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                path=Path(entry.path)
                if entry.is_symlink():raise ValueError('Nested symlink in recovered pack: '+str(path))
                if entry.is_dir(follow_symlinks=False):pending.append(path)
                elif entry.is_file(follow_symlinks=False):yield path
                else:raise ValueError('Unsupported recovered pack entry: '+str(path))


def add_packs(plan,source):
    source=Path(source).resolve(strict=True);records=[];groups={}
    for scope in SCOPES:
        base=source/scope
        if not base.is_dir():continue
        base=base.resolve();plan.protect(base);paths=sorted(files_under(base));groups[scope]=len(paths)
        for path in paths:
            relative='private/'+scope+'/'+path.relative_to(base).as_posix()
            records.append((relative,path,dict(kind='prepared-recovered-data',source=str(path),scope=scope,
                           conversion=None,originality='not inferred; supplied sidecars/provenance copied unchanged')))
    if not records:raise ValueError('No supported recovered data packs found')
    plan.add_files(records,progress=lambda count:print('Checked '+str(count)+' recovered pack files',flush=True))
    recognized={scope.split('/')[0] for scope in SCOPES}
    unselected=[p.name for p in sorted(source.iterdir()) if p.name not in recognized]
    return dict(groups=groups,files=len(records),bytes=sum(plan.entries[row[0]].size for row in records),
                source=str(source),unselected_top_level=unselected,
                behavior='byte-preserving preparation of recovered packs; not a new scene/asset reconstruction',
                guest_or_script_execution=False)
