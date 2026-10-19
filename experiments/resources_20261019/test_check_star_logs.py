#!/usr/bin/env python3
"""Synthetic log fixtures only: no MPI, SDK or hardware calls."""
import json
from pathlib import Path
import tempfile
import unittest
from check_star_logs import KINDS, PHASES, validate

class LogFixtures(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "remote_logs").mkdir()
        (self.root / "run.json").write_text(json.dumps({"peers": 1, "mpi_exit_code": 0,
            "transport": "rm", "rank_hosts": ["node89", "node97"]}))
        (self.root / "mpi.log").write_text("".join(
            "STAR_SETUP rank=%d ranks=2 node=%s bytes=64 topology=star_sequential\n"
            "STAR_CONTENT_PASS rank=%d ranks=2 peers=1\n" % (rank, host, rank)
            for rank, host in ((0, "node89"), (1, "node97"))))
        for rank in (0, 1):
            rows = [{"event": "start", "rank": rank, "pid": rank + 100}]
            for phase in PHASES:
                for kind in sorted(KINDS):
                    rows.append(dict(event="snapshot", seq=len(rows), phase=phase,
                        kind=kind, rank=rank, pid=rank+100, acquired=0, released=0,
                        live=0, peak=0, unique_live=0, valid=1,
                        acquire_failed=0, release_failed=0))
            path = (self.root if rank == 0 else self.root / "remote_logs") / ("rank%d_fixture.jsonl" % rank)
            self.save(path, rows)
        self.center = self.root / "rank0_fixture.jsonl"
    def save(self, path, rows):
        path.write_text("".join(json.dumps(r)+"\n" for r in rows))
    def mutate(self, fn):
        rows = [json.loads(s) for s in self.center.read_text().splitlines()]
        fn(rows)
        self.save(self.center, rows)
    def rejected(self):
        with self.assertRaises(ValueError): validate(self.root, 1)
    def aligned_fixture(self, transport="rm"):
        meta_path = self.root / "run.json"
        meta = json.loads(meta_path.read_text())
        meta.update(profile="depth1024_priority5", transport=transport)
        meta_path.write_text(json.dumps(meta))
        for path in (self.center, self.root / "remote_logs/rank1_fixture.jsonl"):
            rows = [json.loads(x) for x in path.read_text().splitlines()]
            head = rows[0]
            pairs = [("jetty", 256), ("jfr", 1024), ("jfc", 256), ("jfc", 1024)]
            if transport == "rc": pairs += [("jfc", 256), ("jfc", 1024)]
            extra = [dict(event="acquire", kind=k, depth=d, rc=0, valid=1,
                          pid=head["pid"], rank=head["rank"]) for k, d in pairs]
            rows[1:1] = extra
            for i, row in enumerate(rows[1:], 1): row["seq"] = i
            self.save(path, rows)
    def test_aligned_rm(self):
        self.aligned_fixture(); self.assertEqual(validate(self.root, 1)["ranks"][0]["depth_check"], "PASS")
    def test_aligned_rc(self):
        self.aligned_fixture("rc"); self.assertEqual(validate(self.root, 1)["result"], "PASS")
    def test_aligned_wrong_rx(self):
        self.aligned_fixture(); self.mutate(lambda r: r[2].update(depth=4096)); self.rejected()
    def test_aligned_missing_jfc(self):
        self.aligned_fixture(); self.mutate(lambda r: r[3].update(kind="jfs")); self.rejected()
    def test_valid(self): self.assertEqual(validate(self.root, 1)["result"], "PASS")
    def test_missing_rank(self):
        (self.root / "remote_logs/rank1_fixture.jsonl").unlink(); self.rejected()
    def test_invalid_tracker(self):
        self.mutate(lambda r: r[1].update(valid=0)); self.rejected()
    def test_broken_sequence(self):
        self.mutate(lambda r: r[1].update(seq=9)); self.rejected()
    def test_missing_exit(self):
        self.mutate(lambda r: r.pop()); self.rejected()
    def test_failed_release(self):
        self.mutate(lambda r: r[-1].update(release_failed=1)); self.rejected()
    def test_unreleased_resource(self):
        self.mutate(lambda r: r[-1].update(acquired=1, live=1, peak=1, unique_live=1)); self.rejected()
    def test_content_missing(self):
        path = self.root / "mpi.log"
        path.write_text(path.read_text().replace("STAR_CONTENT_PASS rank=1", "MISSING rank=1")); self.rejected()
    def test_wrong_host(self):
        path = self.root / "mpi.log"
        path.write_text(path.read_text().replace("node=node97", "node=node89")); self.rejected()
    def test_outer_timeout(self):
        path = self.root / "run.json"
        row = json.loads(path.read_text()); row["mpi_exit_code"] = 124
        path.write_text(json.dumps(row)); self.rejected()

if __name__ == "__main__": unittest.main()
