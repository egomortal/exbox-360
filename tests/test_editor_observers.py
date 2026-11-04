# SPDX-License-Identifier: GPL-3.0-or-later
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from generate_editor_observers import records,ROOT
class ObserverSpec(unittest.TestCase):
    def setUp(self):self.text=(ROOT/'docs/astra/EDITOR-LAYER-SPEC.md').read_text()
    def test_three_exact_sites(self):
        rows=records(self.text)
        self.assertEqual([r['pc'] for r in rows],['0x9220B2D8','0x9220B038','0x9228E3F0'])
        self.assertEqual([len(r['registers']) for r in rows],[2,2,1])
    def test_duplicate_pc_rejected(self):
        with self.assertRaises(ValueError):records(self.text.replace('0x9220B038 |','0x9220B2D8 |'))
    def test_invalid_register_rejected(self):
        with self.assertRaises(ValueError):records(self.text.replace('r3,r4 | observe_poll','r33,r4 | observe_poll'))
    def test_identifier_injection_rejected(self):
        with self.assertRaises(ValueError):records(self.text.replace('| observe_poll |','| bad(); |'))
if __name__=='__main__':unittest.main()
