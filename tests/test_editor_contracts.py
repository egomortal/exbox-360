# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from editor_contracts import activation_plan, REQUIRED

class Contracts(unittest.TestCase):
    def entry(self, name="status"):
        return dict(id=name, pc=0x92201df8, xex_sha256="a"*64,
                    enabled=True, evidence={key:"fixture proof" for key in REQUIRED})
    def test_disabled_unverified_is_not_activated(self):
        e=self.entry();e.update(enabled=False,evidence={})
        self.assertEqual(activation_plan([e]), [])
    def test_every_evidence_gate_required(self):
        for key in REQUIRED:
            e=self.entry();del e["evidence"][key]
            with self.assertRaises(ValueError):activation_plan([e])
    def test_shared_pc_requires_trace_and_exact_order(self):
        a=self.entry();b=self.entry("null")
        with self.assertRaises(ValueError):activation_plan([a,b])
        a.update(order=1,order_trace="trace-A");b.update(order=0,order_trace="trace-A")
        self.assertEqual([e["id"] for e in activation_plan([a,b])],["null","status"])
        b["order_trace"]="other"
        with self.assertRaises(ValueError):activation_plan([a,b])
    def test_revisions_are_not_merged(self):
        a=self.entry();b=self.entry("other-build");b["xex_sha256"]="b"*64
        self.assertEqual(len(activation_plan([a,b])),2)
    def test_invalid_pc_identity_and_duplicate(self):
        for field,value in [("pc",3),("pc",-4),("xex_sha256","unknown")]:
            e=self.entry();e[field]=value
            with self.assertRaises(ValueError):activation_plan([e])
        e=self.entry()
        with self.assertRaises(ValueError):activation_plan([e,copy.deepcopy(e)])
if __name__=="__main__":unittest.main()
