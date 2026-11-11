"""Branding step checks (no console): python -B tests/test_branding.py"""
import json, sqlite3, sys, tempfile, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import branding as b

class Branding(unittest.TestCase):
    def test_param_name(self):
        src = json.dumps({"titleId": "PPSA99734", "localizedParameters": {"defaultLanguage": "en-US", "en-US": {"titleName": "Old"}}}).encode()
        self.assertEqual(json.loads(b.set_title_name(src, "Ex-Box Avatars"))["localizedParameters"]["en-US"]["titleName"], "Ex-Box Avatars")

    def test_app_db_only_touches_our_titles(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "app.db"; db = sqlite3.connect(p)
            db.execute("create table tbl_contentinfo(titleId text,titleName text,AppInfoJson text)")
            db.execute("create table tbl_conceptmetadata(conceptName text,ConceptInfoJson text)")
            db.execute("insert into tbl_contentinfo values('PPSA99734','AV2 Editor (Bring-up)','{\"TITLE\":\"AV2 Editor (Bring-up)\"}')")
            db.execute("insert into tbl_contentinfo values('PPSA00001','Some Game','{\"TITLE\":\"Some Game\"}')")
            db.execute("insert into tbl_conceptmetadata values('AV2 Editor (Bring-up)','{\"field_list\":[{\"data\":\"AV2 Editor (Bring-up)\",\"key\":\"concept_name\",\"size\":21,\"type\":2}]}')")
            db.commit(); db.close()
            self.assertEqual(b.rename_in_app_db(p, {"PPSA99734": "Ex-Box Avatars"}), 4)
            self.assertEqual(b.rename_in_app_db(p, {"PPSA99734": "Ex-Box Avatars"}), 0)
            db = sqlite3.connect(p)
            self.assertIn('"size":14', db.execute("select ConceptInfoJson from tbl_conceptmetadata").fetchone()[0])
            self.assertEqual(db.execute("select titleName from tbl_contentinfo where titleId='PPSA00001'").fetchone()[0], "Some Game")
            db.close()  # Windows cannot remove the temporary folder while the file is open

    def test_default_icons_exist(self):
        for t, path in b.make_icons("default", None, None, Path(".")).items():
            self.assertTrue(path.is_file(), t)

if __name__ == "__main__":
    unittest.main()
