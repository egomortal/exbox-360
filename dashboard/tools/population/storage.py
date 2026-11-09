"""Plan all outputs first, preserve conflicts, and verify every committed byte."""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
from itertools import islice
from verified_files import copy_verified,sha256


def digest(data):
    return hashlib.sha256(data).hexdigest()


def relative_parts(relative):
    parts=relative.split('/')
    if not parts or any(part in ('','.','..') or any(c in part for c in ':\\\0') for part in parts):
        raise ValueError('Unsafe output path: '+relative)
    reserved={'CON','PRN','AUX','NUL',*('COM'+str(n) for n in range(1,10)),*('LPT'+str(n) for n in range(1,10))}
    if any(part.rstrip(' .')!=part or part.split('.')[0].upper() in reserved for part in parts):
        raise ValueError('Non-portable output path: '+relative)
    return parts


def safe_destination(root,relative,checked_parents=None):
    # Lexical confinement plus verified non-symlink ancestors is sufficient.
    # Resolving the complete absolute path for every closet file repeats the
    # same expensive NTFS ancestor walk thousands of times under WSL.
    path=Path(root).absolute().joinpath(*relative_parts(relative))
    for parent in (path,*path.parents):
        if checked_parents is not None and parent in checked_parents:break
        if parent.is_symlink():raise ValueError('Symlink in output path: '+str(parent))
        if checked_parents is not None and parent!=path:checked_parents.add(parent)
    return path


@dataclass
class Entry:
    path:str
    sha256:str
    size:int
    provenance:dict
    data:bytes|None=None
    source:Path|None=None
    generated:bool=False


def bounded_map(function,items):
    """At most 128 pending records / 8 open-file workers on the desktop only."""
    iterator=iter(items)
    with ThreadPoolExecutor(max_workers=8) as executor:
        while batch:=list(islice(iterator,128)):
            yield from executor.map(function,batch)


def file_entry(record):
    path,source,provenance=record
    source=Path(source)
    if not source.is_file() or source.is_symlink():raise ValueError('Invalid input file: '+str(source))
    return Entry(path,sha256(source),source.stat().st_size,provenance,source=source)


def content_state(item):
    entry,target=item
    source_hash=None
    if entry.source is not None:
        if entry.source.is_symlink() or not entry.source.is_file():raise ValueError('Input changed after planning: '+str(entry.source))
        source_hash=sha256(entry.source)
        if source_hash!=entry.sha256:raise ValueError('Input changed after planning: '+str(entry.source))
    exists=target.exists()
    regular=target.is_file() if exists else False
    if regular:
        actual=source_hash if entry.source is not None and entry.source.absolute()==target else sha256(target)
        return exists,regular,actual==entry.sha256
    return exists,regular,False


class Plan:
    def __init__(self,root):
        raw_root=Path(root).absolute()
        if any(p.is_symlink() for p in (raw_root,*raw_root.parents)):
            raise ValueError('Symlink in output root: '+str(raw_root))
        self.root=raw_root.resolve()
        self.entries={}
        self.folded={}
        self.protected_roots=[]
        self.directories={}

    def protect(self,path):
        self.protected_roots.append(Path(path).resolve())

    def add_directory(self,relative):
        relative_parts(relative);folded=relative.casefold()
        if folded in self.folded or (folded in self.directories and self.directories[folded]!=relative):
            raise ValueError('File/directory or case collision: '+relative)
        self.directories[folded]=relative

    def _add(self,entry):
        relative_parts(entry.path)
        folded=entry.path.casefold()
        if folded in self.directories:raise ValueError('File/directory collision: '+entry.path)
        if folded in self.folded and self.folded[folded]!=entry.path:
            raise ValueError('Case-colliding outputs: '+entry.path)
        if entry.path in self.entries:
            previous=self.entries[entry.path]
            if previous.sha256!=entry.sha256 or previous.size!=entry.size:
                raise ValueError('Conflicting planned outputs: '+entry.path)
            return
        self.folded[folded]=entry.path
        self.entries[entry.path]=entry

    def add_bytes(self,path,data,provenance,generated=False):
        self._add(Entry(path,digest(data),len(data),provenance,data=data,generated=generated))

    def add_file(self,path,source,provenance):
        self._add(file_entry((path,source,provenance)))

    def add_files(self,records,progress=None):
        # Hash independent files concurrently, then commit entries in source
        # order on this thread. No worker mutates the plan or writes anything.
        for count,entry in enumerate(bounded_map(file_entry,records),1):
            self._add(entry)
            if progress and count%5000==0:progress(count)

    def preflight(self):
        """Read-only: all structural/content conflicts precede every write."""
        checked_parents=set();checked_types=set();items=[]
        prefixes={}
        for relative,is_directory in [(e.path,False) for e in self.entries.values()]+[(p,True) for p in self.directories.values()]:
            parts=relative_parts(relative)
            for count in range(1,len(parts)+(1 if is_directory else 0)):
                prefix='/'.join(parts[:count]);fold=prefix.casefold()
                if fold in self.folded:raise ValueError('Planned file used as directory: '+prefix)
                if fold in prefixes and prefixes[fold]!=prefix:raise ValueError('Case-colliding directories: '+prefix)
                prefixes[fold]=prefix
        def check_parent_types(target):
            for parent in target.parents:
                if parent in checked_types:break
                if parent.exists() and not parent.is_dir():raise ValueError('Output parent is a file: '+str(parent))
                checked_types.add(parent)
        for relative in self.directories.values():
            target=safe_destination(self.root,relative,checked_parents)
            check_parent_types(target)
            if target.exists() and not target.is_dir():raise ValueError('Directory output is a file: '+relative)
            if not target.exists() and any(target.is_relative_to(root) for root in self.protected_roots):
                raise ValueError('Directory output would modify an original input tree: '+relative)
        for entry in self.entries.values():
            target=safe_destination(self.root,entry.path,checked_parents)
            check_parent_types(target)
            items.append((entry,target))
        states=bounded_map(content_state,items)
        for (entry,target),(exists,regular,identical) in zip(items,states):
            if not identical and any(target.is_relative_to(root) for root in self.protected_roots):
                raise ValueError('Output would modify an original input tree: '+entry.path)
            if exists:
                if not regular:raise ValueError('Output is not a file: '+entry.path)
                if not identical and not entry.generated:
                    raise ValueError('Existing original/input differs (preserved): '+entry.path)

    def apply(self):
        self.preflight()
        changed=unchanged=0
        backups=[];directories_created=0
        for relative in sorted(self.directories.values()):
            target=safe_destination(self.root,relative)
            if not target.exists():target.mkdir(parents=True,exist_ok=True);directories_created+=1
        for entry in self.entries.values():
            target=self.root.joinpath(*relative_parts(entry.path))
            # This entry is already the verified input file. There is no copy
            # or write to perform, so another traversal/hash adds no operation.
            if entry.source is not None and entry.source.absolute()==target:
                unchanged+=1
                continue
            target=safe_destination(self.root,entry.path)
            if target.exists() and sha256(target)==entry.sha256:
                unchanged+=1
                continue
            if target.exists():
                if not entry.generated:raise ValueError('Output changed after preflight: '+entry.path)
                old=target.read_bytes();old_hash=digest(old)
                backup=safe_destination(self.root,'private/av2/population-backups/'+old_hash)
                backup.parent.mkdir(parents=True,exist_ok=True)
                if not backup.exists():
                    with backup.open('xb') as stream:stream.write(old)
                if sha256(backup)!=old_hash:raise ValueError('Backup verification failed: '+entry.path)
                backups.append(dict(path=entry.path,sha256=old_hash,backup=str(backup.relative_to(self.root))))
            target.parent.mkdir(parents=True,exist_ok=True)
            if entry.source is not None:
                # Reuse the existing streaming copier; do not load the closet into RAM.
                result=copy_verified(entry.source,target)
                if result['sha256']!=entry.sha256:raise ValueError('Input changed during setup: '+str(entry.source))
            else:
                pending=target.with_name(target.name+'.population-'+entry.sha256[:16]+'.pending')
                if pending.exists():raise ValueError('Preserved interrupted output: '+str(pending))
                with pending.open('xb') as stream:stream.write(entry.data)
                if sha256(pending)!=entry.sha256:raise ValueError('Pending output hash mismatch: '+entry.path)
                pending.replace(target)
            if target.stat().st_size!=entry.size or sha256(target)!=entry.sha256:
                raise ValueError('Output verification failed: '+entry.path)
            changed+=1
        return dict(changed=changed,unchanged=unchanged,directories_created=directories_created,backups=backups)

    def manifest(self):
        return [dict(path=e.path,bytes=e.size,sha256=e.sha256,generated=e.generated,provenance=e.provenance)
                for e in sorted(self.entries.values(),key=lambda item:item.path)]

    def manifest_bytes(self):
        return (json.dumps(self.manifest(),indent=2,sort_keys=True)+'\n').encode('utf-8')
