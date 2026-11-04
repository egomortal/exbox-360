"""Population byte identity, idempotency, failure boundaries and cache reuse."""
from pathlib import Path
import hashlib,importlib.util,json,os,struct,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/xur5')]
from population.storage import Plan,safe_destination
from population.archives import ArchiveCache
from population.workflow import add_editor,add_shared
from test_xuiz_archive import archive
import xuiz


class StorageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='population test ');self.root=Path(self.temp.name)/'out'
    def tearDown(self):self.temp.cleanup()
    def test_exact_bytes_and_repeat_is_no_write(self):
        p=Plan(self.root);data=bytes(range(256));p.add_bytes('private/x.png',data,{'kind':'original'})
        self.assertEqual(p.apply()['changed'],1);path=self.root/'private/x.png';stamp=path.stat().st_mtime_ns
        self.assertEqual(path.read_bytes(),data);self.assertEqual(p.apply()['changed'],0);self.assertEqual(path.stat().st_mtime_ns,stamp)
    def test_late_conflict_prevents_all_writes(self):
        self.root.mkdir();(self.root/'conflict').write_bytes(b'existing')
        p=Plan(self.root);p.add_bytes('new',b'new',{});p.add_bytes('conflict',b'different',{})
        with self.assertRaises(ValueError):p.apply()
        self.assertFalse((self.root/'new').exists());self.assertEqual((self.root/'conflict').read_bytes(),b'existing')
    def test_generated_update_has_verified_backup(self):
        self.root.mkdir();(self.root/'generated').write_bytes(b'old')
        p=Plan(self.root);p.add_bytes('generated',b'new',{},generated=True);result=p.apply()
        backup=self.root/result['backups'][0]['backup'];self.assertEqual(backup.read_bytes(),b'old')
        self.assertEqual((self.root/'generated').read_bytes(),b'new');self.assertEqual(p.apply()['changed'],0)
    def test_no_output_for_changed_source(self):
        source=Path(self.temp.name)/'source';source.write_bytes(b'original')
        p=Plan(self.root);p.add_bytes('first',b'x',{});p.add_file('second',source,{})
        source.write_bytes(b'changed')
        with self.assertRaises(ValueError):p.apply()
        self.assertFalse(self.root.exists())
    def test_original_tree_protected_even_for_generated(self):
        p=Plan(self.root);p.protect(self.root);p.add_bytes('generated',b'new',{},generated=True)
        with self.assertRaises(ValueError):p.apply()
        self.assertFalse(self.root.exists())
    def test_protected_original_allows_noop_verification(self):
        self.root.mkdir();(self.root/'original').write_bytes(b'same')
        p=Plan(self.root);p.protect(self.root);p.add_file('original',self.root/'original',{})
        self.assertEqual(p.apply()['changed'],0)
    def test_unsafe_paths_and_case_collisions(self):
        for path in ['../escape','/absolute','a/../../b','C:/x','a\\b','a//b','a/./b','a/','x\0y','a/CON.png','a/name.','a/name ']:
            with self.subTest(path=path),self.assertRaises(ValueError):safe_destination(self.root,path)
        p=Plan(self.root);p.add_bytes('File',b'a',{})
        with self.assertRaises(ValueError):p.add_bytes('file',b'a',{})
    def test_duplicate_conflict_rejected(self):
        p=Plan(self.root);p.add_bytes('same',b'a',{})
        with self.assertRaises(ValueError):p.add_bytes('same',b'b',{})
    def test_symlink_output_rejected(self):
        self.root.mkdir();outside=Path(self.temp.name)/'elsewhere';outside.mkdir()
        try:(self.root/'linked').symlink_to(outside,target_is_directory=True)
        except (OSError,NotImplementedError):self.skipTest('Host cannot create symlinks')
        p=Plan(self.root)
        p.add_bytes('linked/file',b'a',{})
        with self.assertRaises(ValueError):p.preflight()
        self.assertFalse((outside/'file').exists())
    def test_file_parent_fails_before_write(self):
        self.root.mkdir();(self.root/'parent').write_bytes(b'a');p=Plan(self.root)
        p.add_bytes('new',b'a',{});p.add_bytes('parent/child',b'a',{})
        with self.assertRaises((ValueError,OSError)):p.apply()
        self.assertFalse((self.root/'new').exists())
    def test_empty_directory_preserved_and_repeat_is_no_write(self):
        p=Plan(self.root);p.add_directory('private/assets/empty')
        self.assertEqual(p.apply()['directories_created'],1)
        self.assertTrue((self.root/'private/assets/empty').is_dir())
        self.assertEqual(p.apply()['directories_created'],0)

    def test_planned_file_cannot_be_a_directory(self):
        p=Plan(self.root);p.add_bytes('a',b'x',{});p.add_bytes('a/child',b'y',{})
        with self.assertRaises(ValueError):p.apply()
        self.assertFalse(self.root.exists())
        p=Plan(self.root);p.add_bytes('Dir/x',b'x',{});p.add_bytes('dir/y',b'y',{})
        with self.assertRaises(ValueError):p.apply()
        self.assertFalse(self.root.exists())
    def test_empty_directory_parent_conflict_precedes_writes(self):
        self.root.mkdir();(self.root/'blocked').write_bytes(b'old')
        p=Plan(self.root);p.add_directory('new');p.add_directory('blocked/empty')
        with self.assertRaises(ValueError):p.apply()
        self.assertFalse((self.root/'new').exists())

    def test_manifest_order_stable(self):
        p=Plan(self.root);p.add_bytes('b',b'b',{});p.add_bytes('a',b'a',{})
        self.assertEqual([r['path'] for r in json.loads(p.manifest_bytes())],['a','b'])


class ArchiveAndCliTests(unittest.TestCase):
    def test_cache_reads_and_decodes_once_for_both_views(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'package';source.write_bytes(archive([('a\\b',b'one'),('a\\b',b'two')]))
            cache=ArchiveCache()
            for _ in range(5):
                self.assertEqual(cache.lookup(source),{'a\\b':b'two'})
                self.assertEqual(cache.lookup(source,True),{'a/b':b'two'})
            self.assertEqual(cache.statistics()['unique_reads'],1);self.assertEqual(cache.statistics()['unique_decodes'],1)
            cache.note(source,'a\\b','runtime')
            evidence=cache.evidence()[0]
            self.assertEqual(len(evidence['members']),2)
            self.assertEqual(evidence['members'][0]['roles'],[])
            self.assertTrue(evidence['members'][0]['shadowed_by_later_name'])
            self.assertEqual(evidence['members'][1]['roles'],['runtime'])
    def test_original_shared_archive_needs_no_pre_extraction(self):
        with tempfile.TemporaryDirectory() as folder:
            shared=Path(folder)/'shrdres.xzp';shared.write_bytes(archive([('icon.png',bytes(range(256)))]))
            plan=Plan(Path(folder)/'out');cache=ArchiveCache();add_shared(plan,shared,'icon.png',cache);plan.apply()
            self.assertEqual((plan.root/'private/xbox/shrdres/icon.png').read_bytes(),bytes(range(256)))
            self.assertEqual(cache.statistics()['unique_decodes'],1)

    def test_extraction_original_last_duplicate_and_repeat(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'package';out=Path(folder)/'out';source.write_bytes(archive([('a',b'first'),('a',b'last'),('dir/image',bytes(range(256)))]))
            xuiz.unpack(source,out);self.assertEqual((out/'a').read_bytes(),b'last');self.assertEqual((out/'dir/image').read_bytes(),bytes(range(256)))
            self.assertEqual(xuiz.unpack(source,out)['changed'],0)
    def test_extraction_rejects_escape_before_valid_member_write(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'package';out=Path(folder)/'out';source.write_bytes(archive([('valid',b'a'),('../escape',b'bad')]))
            with self.assertRaises(ValueError):xuiz.unpack(source,out)
            self.assertFalse(out.exists());self.assertFalse((Path(folder)/'escape').exists())
    def test_stale_era_image_preserved_before_population(self):
        from unittest.mock import patch
        from population.workflow import add_dashboard,library
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);resources=root/'resources';resources.mkdir()
            for directory in library.BUILDS.values():(resources/directory/'dash.xex').mkdir(parents=True)
            project=root/'project';project.mkdir();(project/'main.cpp').write_text('unused')
            output=root/'out';stale=output/'private/era-library/stale.png';stale.parent.mkdir(parents=True);stale.write_bytes(b'preserve')
            plan=Plan(output)
            with patch.object(library,'used_paths',return_value=[]),patch.object(library,'collect',return_value=({},[])):
                with self.assertRaisesRegex(ValueError,'Unmapped existing era artwork'):
                    add_dashboard(plan,project,resources,root/'shared',ArchiveCache())
            self.assertEqual(stale.read_bytes(),b'preserve')
            self.assertFalse((output/'media_transport_layout.hpp').exists())

    def test_wrong_editor_pair_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'editor';(source/'assets').mkdir(parents=True);(source/'assets/avatareditor.xex').write_bytes(b'wrong version')
            p=Plan(Path(folder)/'out')
            with self.assertRaises(ValueError):add_editor(p,source)
            self.assertFalse(p.root.exists())
    def test_setup_help_and_missing_inputs_from_other_cwd(self):
        with tempfile.TemporaryDirectory() as folder:
            env={**os.environ,'EXBOX_INPUTS':str(Path(folder)/'missing'),'PYTHONDONTWRITEBYTECODE':'1'}
            result=subprocess.run([sys.executable,str(ROOT/'setup.py'),'--help'],cwd=folder,env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([sys.executable,str(ROOT/'setup.py'),'--check'],cwd=folder,env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode,2);self.assertIn('No supported inputs found',result.stderr)
            self.assertFalse((Path(folder)/'missing').exists())


class IndexTests(unittest.TestCase):
    def test_native_index_legacy_golden_bytes(self):
        from build_av2_asset_index import rows_from_entries,encode,HEADER,RECORD,MAGIC
        data=encode(rows_from_entries([('empty',0,2),('nested',0,2),('nested/data.bin',3,1),('file.bin',5,1)]))
        self.assertEqual(hashlib.sha256(data).hexdigest(),'6f14054111ae0075025981edc28d6b9ad6eb288f68b83fc6edaefa637679ee15')
        self.assertEqual(HEADER.unpack_from(data),(MAGIC,1040,4,0))
        self.assertEqual(len(data),24+4*1040)
    def test_index_excludes_itself_and_rejects_ambiguous_names(self):
        from build_av2_asset_index import rows_from_entries
        self.assertEqual(rows_from_entries([('.av2-index',1,1),('.av2-index.tmp',2,1)]),[])
        for entries in [[('A',1,1),('a',0,2)],[('../escape',1,1)],[('x',1,0)]]:
            with self.assertRaises(ValueError):rows_from_entries(entries)


if __name__=='__main__':unittest.main()
