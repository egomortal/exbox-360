"""Keep preparation inputs independent of the original developer's paths."""
from pathlib import Path
import os,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from input_paths import input_root,dashboard_resources,prepared_editor


class InputPathsTests(unittest.TestCase):
    def test_checkout_default(self):
        root=Path('/a checkout with spaces')
        self.assertEqual(input_root(root,{}),root/'inputs')
        self.assertEqual(dashboard_resources(root,{}),root/'inputs/dashboard-resources')
        self.assertEqual(prepared_editor(root,{}),root/'inputs/avatar-editor')
    def test_explicit_override(self):
        with tempfile.TemporaryDirectory(prefix='exbox inputs ') as folder:
            env={'EXBOX_INPUTS':folder};root=Path('/elsewhere')
            self.assertEqual(input_root(root,env),Path(folder).resolve())
            self.assertEqual(dashboard_resources(root,env),Path(folder).resolve()/'dashboard-resources')
            self.assertEqual(prepared_editor(root,env),Path(folder).resolve()/'avatar-editor')
    def test_entry_points_help_from_unrelated_cwd(self):
        with tempfile.TemporaryDirectory() as cwd:
            for name in ['build_era_library.py','build_media_transport_metrics.py','avatar_editor/stage.py']:
                result=subprocess.run([sys.executable,str(ROOT/'tools'/name),'--help'],cwd=cwd,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertIn('--resources' if 'stage.py' not in name else '--source',result.stdout)
    def test_missing_input_fails_before_output(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'output'
            result=subprocess.run([sys.executable,str(ROOT/'tools/build_media_transport_metrics.py'),'--resources',str(Path(folder)/'absent'),'--output',str(out)],capture_output=True,text=True)
            self.assertEqual(result.returncode,2)
            self.assertIn('Dashboard resources not found',result.stderr)
            self.assertFalse(out.exists())


if __name__=='__main__':unittest.main()
