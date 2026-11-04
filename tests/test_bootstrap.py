"""Bootstrap failure boundaries and byte-preserving preparation contracts."""
from pathlib import Path
import json,struct,sys,tempfile,unittest,zipfile
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from population.raw_updates import read_zip,xex_metadata,recover_xex
from population.prepared_packs import add_packs
from population.storage import Plan
from population.bootstrap import recover_inputs
from prepare_av2_title_port import transform
from audit_source_export import audit

class BootstrapTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
 def tearDown(self):self.temp.cleanup()
 def zip(self,names):
  path=self.root/'original.zip'
  with zipfile.ZipFile(path,'w') as stream:
   for name,data in names:stream.writestr(name,data)
  return path
 def test_zip_preserves_binary_and_repeat(self):
  data=bytes(range(256));source=self.zip([('a/b.xex',data)])
  root,identity,entries=read_zip(source,self.root/'out')
  target=root/entries[0]['path'];stamp=target.stat().st_mtime_ns
  self.assertEqual(target.read_bytes(),data)
  read_zip(source,self.root/'out');self.assertEqual(target.stat().st_mtime_ns,stamp)
 def test_unsafe_zip_has_no_partial_output(self):
  for name in ['../outside','C:/outside','a/CON.xex','a/../b']:
   source=self.zip([('valid',b'safe'),(name,b'unsafe')])
   with self.assertRaises(ValueError):read_zip(source,self.root/'out')
   self.assertFalse((self.root/'out').exists())
 def test_symlink_zip_refused(self):
  source=self.root/'link.zip';info=zipfile.ZipInfo('link');info.external_attr=0o120777<<16
  with zipfile.ZipFile(source,'w') as stream:stream.writestr(info,'target')
  with self.assertRaises(ValueError):read_zip(source,self.root/'out')
 def test_zip_late_conflict_preserves_original(self):
  source=self.zip([('original',b'a')]);root,_,_=read_zip(source,self.root/'out')
  (root/'original/original').write_bytes(b'changed')
  with self.assertRaises(ValueError):read_zip(source,self.root/'out')
  self.assertEqual((root/'original/original').read_bytes(),b'changed')
 def test_xex_bounds_and_leaf_names(self):
  with self.assertRaises(ValueError):xex_metadata(b'XEX2')
  data=bytearray(64);data[:4]=b'XEX2';struct.pack_into('>I',data,20,1)
  struct.pack_into('>II',data,24,0x2ff,32);struct.pack_into('>I',data,32,20);data[36:44]=b'../bad\0\0'
  with self.assertRaises(ValueError):xex_metadata(bytes(data))
 def test_xex_version_and_delta_no_tool_execution(self):
  data=bytearray(80);data[:4]=b'XEX2';struct.pack_into('>I',data,20,2)
  struct.pack_into('>II',data,24,0x40006,40);struct.pack_into('>II',data,32,0x3ff,64)
  struct.pack_into('>I',data,44,(2<<28)|(17559<<8)|7);struct.pack_into('>HH',data,68,1,3)
  self.assertEqual(xex_metadata(data)['execution_version'],dict(major=2,minor=0,build=17559,qfe=7))
  source=self.root/'delta.xexp';source.write_bytes(data)
  with patch('population.raw_updates.subprocess.run') as run:
   result=recover_xex(source,self.root/'out','never-execute')
  self.assertEqual(result['status'],'requires-delta-base');run.assert_not_called()
 def test_binary_title_input_never_decoded(self):
  data=b'\xff\xfe\x00\x80';self.assertEqual(transform('embedded.bin',data),data)
 def test_pack_bytes_and_protected_scopes(self):
  source=self.root/'packs';(source/'promotions').mkdir(parents=True)
  data=b'\xfforiginal compressed bytes\0';(source/'promotions/ad.xzp').write_bytes(data)
  (source/'avatar').mkdir();(source/'avatar/keep').write_bytes(b'protected')
  (source/'trailers').mkdir();(source/'trailers/video.mp4').write_bytes(b'separate media')
  plan=Plan(self.root/'out');summary=add_packs(plan,source)
  self.assertEqual(summary['files'],1);plan.apply()
  self.assertEqual((self.root/'out/private/promotions/ad.xzp').read_bytes(),data)
  self.assertFalse((self.root/'out/private/avatar').exists())
  self.assertFalse((self.root/'out/private/trailers').exists())
 def test_raw_check_creates_nothing(self):
  modules=self.root/'modules';modules.mkdir();tool=self.root/'tool';tool.mkdir()
  result=recover_inputs(self.root,self.root,['dashboard'],modules,self.root/'guides',self.root/'updates',tool,self.root/'missing',True)
  self.assertTrue(result['pending_dashboard']);self.assertFalse((self.root/'private').exists())
 def test_raw_external_output_refused_before_work(self):
  with self.assertRaises(ValueError):recover_inputs(self.root,self.root/'out',['dashboard'],self.root,self.root,self.root,self.root,self.root/'missing',False)
 def test_source_audit_excludes_payloads_and_detects_embeds(self):
  (self.root/'private').mkdir();(self.root/'private/secret.py').write_text('not exportable')
  (self.root/'original.xex').write_bytes(b'XEX2')
  (self.root/'ok.py').write_text('pattern="data:image/"')
  (self.root/'embedded.py').write_text('value="data:image/png;base64,'+'A'*128+'"')
  report=audit(self.root)
  self.assertEqual([r['path'] for r in report['source_candidates']],['ok.py'])
  self.assertEqual([r['path'] for r in report['review_required']],['embedded.py'])
 def test_core_transform_preserves_unrelated_source_and_binary(self):
  from prepare_av2_ps5_core import transform as core_transform
  for name,data in [('input.bin',b'\xff\0\xfe'),('src/core/unrelated.cpp',b'// unchanged\r\n')]:
   self.assertEqual(core_transform(name,data),data)
 def test_zip_crc_failure_has_no_partial_outputs(self):
  source=self.zip([('first',b'safe'),('second',b'unique crc test payload')])
  data=source.read_bytes().replace(b'unique crc test payload',b'Xnique crc test payload')
  source.write_bytes(data)
  with self.assertRaisesRegex(zipfile.BadZipFile,'Bad CRC-32'):read_zip(source,self.root/'out')
  self.assertFalse((self.root/'out').exists())
 def test_portable_title_recipe_has_no_owner_paths(self):
  recipe=(ROOT/'tools/native_editor/title-recipe.json').read_text()
  self.assertNotIn('/mnt/c/',recipe);self.assertNotIn('Users',recipe)
  self.assertEqual(len(json.loads(recipe)['units']),38)

if __name__=='__main__':unittest.main()
