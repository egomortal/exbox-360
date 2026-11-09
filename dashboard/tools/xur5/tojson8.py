"""tojson8.py IN.xur OUT.json: decode an XUIB version 8 scene to the JSON tree shape of tojson.py."""
import sys,json
sys.path.insert(0,str(__import__('pathlib').Path(__file__).resolve().parent))
import xur8
r,d=xur8.decode(sys.argv[1])
json.dump(r,open(sys.argv[2],'w'),indent=1)
