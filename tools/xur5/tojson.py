import sys,json
sys.path.insert(0,str(__import__('pathlib').Path(__file__).resolve().parent))
from xur5 import *
from classes5 import *
d=D(sys.argv[1],L);r=d.decode();assert d.at==d.end,(d.at,d.end)
json.dump(r,open(sys.argv[2],'w'),indent=1)
