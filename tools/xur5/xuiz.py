"""Extract XUIZ members unchanged using the shared reader and verified writer."""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from xuiz_archive import members
from population.storage import Plan


def unpack(path,out):
    path=Path(path);raw=path.read_bytes();entries=list(members(raw,normalize_slashes=True))
    plan=Plan(out)
    # Original extraction used last-entry-wins names. Keep that explicit while
    # the source archive retains every duplicate member and its original bytes.
    for name,data in dict(entries).items():
        plan.add_bytes(name,data,dict(kind='original-input-member',source=str(path),
                       archive_sha256=hashlib.sha256(raw).hexdigest(),member=name,conversion=None))
    plan.preflight()
    result=plan.apply()
    print(path,struct.unpack_from('>I',raw,4)[0],len(entries))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('packages',type=Path,nargs='+')
    args=parser.parse_args()
    for path in args.packages:unpack(path,args.output/path.name)


if __name__=='__main__':main()
