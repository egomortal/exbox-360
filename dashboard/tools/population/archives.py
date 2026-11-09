"""Keep each source archive and its decoded members once per setup invocation."""
from pathlib import Path
import hashlib
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'xur5'))
from xuiz_archive import members


class ArchiveCache:
    def __init__(self):
        self.raw={}
        self.tables={}
        self.lookups={}
        self.reads=0
        self.parses=0
        self.requests=0
        self.used={}

    def read(self,path):
        path=Path(path).resolve()
        self.requests+=1
        if path not in self.raw:
            self.raw[path]=path.read_bytes()
            self.reads+=1
        return self.raw[path]

    def lookup(self,path,normalize_slashes=False):
        path=Path(path).resolve()
        if path not in self.tables:
            self.tables[path]=tuple(members(self.read(path)))
            self.parses+=1
        key=(path,normalize_slashes)
        if key not in self.lookups:
            self.lookups[key]={name.replace('\\','/') if normalize_slashes else name:data
                               for name,data in self.tables[path]}
        return self.lookups[key]

    def note(self,path,member,role):
        key=(Path(path).resolve(),member.replace('\\','/'))
        self.used.setdefault(key,set()).add(role)

    def evidence(self):
        records=[]
        for path,data in sorted(self.raw.items()):
            record=dict(path=str(path),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))
            if path in self.tables:
                # Retain every ordered member, including unselected/duplicate
                # resources. Presence is not proof of a resolved runtime mapping.
                last={name.replace('\\','/'):index for index,(name,_) in enumerate(self.tables[path])}
                record['members']=[dict(index=index,name=name,bytes=len(payload),
                    sha256=hashlib.sha256(payload).hexdigest(),
                    shadowed_by_later_name=index!=last[name.replace('\\','/')],
                    roles=sorted(self.used.get((path,name.replace('\\','/')),set())) if index==last[name.replace('\\','/')] else [])
                    for index,(name,payload) in enumerate(self.tables[path])]
                record['unselected_members']=sum(not row['roles'] for row in record['members'])
            records.append(record)
        return records

    def statistics(self):
        return dict(unique_reads=self.reads,unique_decodes=self.parses,read_requests=self.requests)
