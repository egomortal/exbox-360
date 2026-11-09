from pathlib import Path
from urllib.request import urlopen, Request
import json

OUT=Path(__file__).resolve().parent.parent / "era-reference" / "audit-20261025"
SOURCES={
    "designer-blades-system.jpg":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343188906405-1KEHRV63BWDAHLCVJBBH/Dash04_system.jpg",
    "designer-blades-live.jpg":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343188905779-4XE6EXJWZKCPEE76RIVQ/Dash01_live.jpg",
    "designer-blades-music.jpg":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343189084189-YMYWFQDOEP75TBT6RNK2/Dash03_music.jpg",
    "designer-nxe-home.jpg":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343190451090-09FJUIAZ2Y5GTJ2EOOUZ/Dash_nxe.jpeg",
    "designer-blades-games.jpg":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343188906235-4MKVV6D1MS0GFMVWN0Y2/Dash02_games.jpg",
    "designer-nxe.jpg":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343192331934-1FX6ZYTEU2SN9BU36Y78/xbox360_nxe.jpg",
    "designer-metro-apps.png":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343190423993-Y1UVNUZSAAFXO43ZJ3SZ/Dash2012_apps.png",
    "designer-guide.jpg":"https://images.squarespace-cdn.com/content/v1/500e2d14e4b042e8cb25252f/1343188920570-4OSA2TGRRFJBHUDQZ9V4/Guide01_main.jpg",
}
OUT.mkdir(parents=True,exist_ok=True)
for name,url in SOURCES.items():
    path=OUT/name
    if not path.exists():
        with urlopen(Request(url,headers={"User-Agent":"Mozilla/5.0"}),timeout=30) as response:
            path.write_bytes(response.read())
    print(name,path.stat().st_size)
(OUT/"sources.json").write_text(json.dumps({"page":"https://www.i4design.com/xbox-360","images":SOURCES},indent=2),encoding="utf-8")
