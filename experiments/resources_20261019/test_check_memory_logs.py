#!/usr/bin/env python3
import copy
import json
from pathlib import Path
import tempfile
import unittest
from check_memory_logs import validate_memory, PHASES
class MemoryFixtures(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.resource = {"ranks": [{"rank":0,"process_exit":{"jetty":{"pid":100}}}]}
        self.rows = [dict(rank=0,pid=100,phase=phase,unit="KiB",rss=100+i,pss=80+i,
            private_clean=10,private_dirty=20+i,private_sum=30+i,vm_size=1000,time_ns=i)
            for i, phase in enumerate(PHASES)]
    def run_rows(self):
        (self.root / "mpi.log").write_text("".join("STAR_MEMORY "+json.dumps(r)+"\n" for r in self.rows))
        return validate_memory(self.root,self.resource)
    def test_valid_delta(self):
        r=self.run_rows();self.assertEqual(r["ranks"][0]["delta_KiB"]["exchange_minus_init"]["rss"],1)
    def test_missing_phase(self):
        self.rows.pop()
        with self.assertRaises(ValueError):self.run_rows()
    def test_wrong_pid(self):
        self.rows[0]["pid"]=999
        with self.assertRaises(ValueError):self.run_rows()
    def test_wrong_unit(self):
        self.rows[0]["unit"]="bytes"
        with self.assertRaises(ValueError):self.run_rows()
    def test_bad_sum(self):
        self.rows[0]["private_sum"]=999
        with self.assertRaises(ValueError):self.run_rows()
    def test_negative_delta_allowed(self):
        self.rows[2]["rss"]=50
        self.assertEqual(self.run_rows()["ranks"][0]["delta_KiB"]["exchange_minus_init"]["rss"],-51)
if __name__ == "__main__": unittest.main()
