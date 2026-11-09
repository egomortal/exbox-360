import sys,tempfile,unittest,hashlib,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from population.intake import intake,identify
class IntakeTests(unittest.TestCase):
 def test_known_copy_repeat_and_check(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);source=root/'anything';data=b'XEX2synthetic';source.write_bytes(data);digest=hashlib.sha256(data).hexdigest();registry=({digest:[dict(path='Blades/Retail/1888/dash.xex')]},{})
   result=intake([source],root/'inputs',True,registry);self.assertFalse((root/'inputs').exists())
   intake([source],root/'inputs',False,registry);target=root/'inputs/dashboard-source/Blades/Retail/1888/dash.xex';self.assertEqual(target.read_bytes(),data)
   before=target.stat().st_mtime_ns;intake([source],root/'inputs',False,registry);self.assertEqual(before,target.stat().st_mtime_ns)
   target.write_bytes(b'conflict')
   with self.assertRaises(ValueError):intake([source],root/'inputs',False,registry)
   self.assertEqual(source.read_bytes(),data)
 def test_unknown_and_corrupt(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);p=root/'x.zip';p.write_bytes(b'bad');self.assertEqual(identify(p,{}, {})['status'],'damaged')
   p=root/'x.txt';p.write_text('unknown');result=intake([p],root/'inputs',registry=({},{}));self.assertEqual(result['files'][0]['status'],'unknown')
 def test_path_escape(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);p=root/'file';p.write_bytes(b'XEX2fake');digest=hashlib.sha256(p.read_bytes()).hexdigest()
   with self.assertRaises(ValueError):intake([p],root/'inputs',registry=({digest:[dict(path='../../escape')]},{}))
 def test_media_headers(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);p=root/'not.mp4';p.write_bytes(b'not a movie');self.assertEqual(identify(p,{}, {})['status'],'unknown')
   p.write_bytes(b'\x00\x00\x00\x18ftypisom');self.assertEqual(identify(p,{}, {})['kind'],'trailers')
if __name__=='__main__':unittest.main()
