import json
from pathlib import Path
import tempfile
import unittest
from check_segment_logs import validate_segments
class Fixtures(unittest.TestCase):
    def setUp(self):
        self.t=tempfile.TemporaryDirectory();self.addCleanup(self.t.cleanup);self.root=Path(self.t.name)
        self.resource={"ranks":[{"rank":0,"process_exit":{"jetty":{"pid":100}}}]}
        base=dict(pid=100,rank=0,valid=1,rc=0,register_failed=0,unregister_failed=0,
            sampled=1,rss_before_KiB=100,rss_after_KiB=104,peak=1)
        self.rows=[dict(base,event="register",seq=1,length=4096,registered=1,unregistered=0,
                       live=1,bytes_live=4096,bytes_total=4096),
                   dict(base,event="unregister",seq=2,length=4096,registered=1,unregistered=1,
                       live=0,bytes_live=0,bytes_total=4096),
                   dict(base,event="process_exit",seq=3,length=0,registered=1,unregistered=1,
                       live=0,bytes_live=0,bytes_total=4096,sampled=0)]
    def run_rows(self):
        (self.root/"segment_rank0_test.jsonl").write_text(''.join(json.dumps(r)+'\n' for r in self.rows))
        return validate_segments(self.root,self.resource)
    def test_valid(self):self.assertEqual(self.run_rows()["result"],"PASS")
    def test_bad_bytes(self):
        self.rows[0]["bytes_total"]=999
        with self.assertRaises(ValueError):self.run_rows()
    def test_missing_exit(self):
        self.rows.pop()
        with self.assertRaises(ValueError):self.run_rows()
    def test_invalid(self):
        self.rows[-1]["valid"]=0
        with self.assertRaises(ValueError):self.run_rows()
    def test_wrong_pid(self):
        self.rows[0]["pid"]=999
        with self.assertRaises(ValueError):self.run_rows()
if __name__=='__main__':unittest.main()
