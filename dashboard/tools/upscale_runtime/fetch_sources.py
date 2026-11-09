# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch only pinned official source and generic model data; no prebuilt programs."""
from pathlib import Path
import urllib.request,json,hashlib,tarfile
O=Path(__file__).resolve().parent
FILES=[{'path': 'upstream/ncnn-source.tar.gz', 'url': 'https://codeload.github.com/Tencent/ncnn/tar.gz/56775de50990ab7f16627efdcf5529b49541206f', 'bytes': 12841427, 'sha256': '25d1a616bb3a598535545e217eb6fc5df98c4ba04344b48dbb141bbb108801ef'}, {'path': 'upstream/realesrgan/LICENSE', 'url': 'https://raw.githubusercontent.com/xinntao/Real-ESRGAN/a4abfb2979a7bbff3f69f58f58ae324608821e27/LICENSE', 'bytes': 1519, 'sha256': '4a699ec4863d96a91fc265948a0c90033f7e8735d515524dcf3444736406e0c2'}, {'path': 'upstream/realesrgan/realesrgan/archs/srvgg_arch.py', 'url': 'https://raw.githubusercontent.com/xinntao/Real-ESRGAN/a4abfb2979a7bbff3f69f58f58ae324608821e27/realesrgan/archs/srvgg_arch.py', 'bytes': 2722, 'sha256': '7419a75e789ca09295266563d74a6d7c08d6c24502af6b0a919b1811d0a0be59'}, {'path': 'upstream/realesrgan/inference_realesrgan.py', 'url': 'https://raw.githubusercontent.com/xinntao/Real-ESRGAN/a4abfb2979a7bbff3f69f58f58ae324608821e27/inference_realesrgan.py', 'bytes': 7745, 'sha256': '389af1172b8ae830dc940283a5e7197d05ab6d9ec15c673bb383c03d957f84bf'}, {'path': 'upstream/models/realesr-general-x4v3.pth', 'url': 'https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth', 'bytes': 4885111, 'sha256': '8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292'}]
rows=[]
for item in FILES:
 p=O/item['path'];p.parent.mkdir(parents=True,exist_ok=True)
 if p.exists():data=p.read_bytes()
 else:
  request=urllib.request.Request(item['url'],headers={'User-Agent':'ExBox-local-build'})
  with urllib.request.urlopen(request,timeout=120) as response:data=response.read(64*1024*1024+1)
 if len(data)!=item['bytes'] or hashlib.sha256(data).hexdigest()!=item['sha256']:raise ValueError('Pinned upstream integrity check failed: '+item['path'])
 if not p.exists():p.write_bytes(data)
 rows.append(item)
with tarfile.open(O/'upstream/ncnn-source.tar.gz') as archive:archive.extractall(O/'upstream',filter='data')
(O/'ncnn-source-path.txt').write_text(str(O/'upstream/ncnn-56775de50990ab7f16627efdcf5529b49541206f'))
(O/'upstream-downloads.json').write_text(json.dumps(rows,indent=2)+'\n');print('Verified pinned source/model files',len(rows))
