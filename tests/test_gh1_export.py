import sys,tempfile,unittest,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from gh1_export import permitted,verify
class ExportTests(unittest.TestCase):
 def test_forbidden(self):
  for path in ['private/x.cpp','inputs/a.h','generated/code.cpp','assets/font.h','../x.py','modules/avatar_editor/native/main.cpp','third_party/a.h','a.spv','a.png']:
   self.assertFalse(permitted(path),path)
 def test_original_hash_disguise(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);data=b'original bytes renamed';(root/'fake.cpp').write_bytes(data);digest=hashlib.sha256(data).hexdigest()
   with self.assertRaises(ValueError):verify(root,[dict(path='fake.cpp',sha256=digest,bytes=len(data))],{digest})
 def test_binary_disguise(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);data=b'XEX2bad';(root/'fake.cpp').write_bytes(data)
   with self.assertRaises(ValueError):verify(root,[dict(path='fake.cpp',sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))],set())
 def test_changed(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);(root/'test.py').write_text('pass')
   with self.assertRaises(ValueError):verify(root,[dict(path='test.py',sha256='0'*64,bytes=4)],set())
if __name__=='__main__':unittest.main()
