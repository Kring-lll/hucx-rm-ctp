import argparse
import os
import unittest
from unittest.mock import patch
from pathlib import Path
from run_osu_compare import environment, parse_output, plan

HEADER='# OSU MPI Latency Test v7.5.2\n# Size Avg Latency(us)\n'
class Checks(unittest.TestCase):
    def test_valid_grid(self):
        self.assertEqual(len(parse_output(HEADER+'1 2.0\n2 3.0\n4 4.0\n',1,4)),3)
    def test_missing_row(self):
        with self.assertRaises(ValueError): parse_output(HEADER+'1 2\n4 3\n',1,4)
    def test_duplicate(self):
        with self.assertRaises(ValueError): parse_output(HEADER+'1 2\n1 3\n',1,1)
    def test_nonfinite(self):
        with self.assertRaises(ValueError): parse_output(HEADER+'1 nan\n',1,1)
    def test_validation(self):
        self.assertEqual(parse_output(HEADER+'64 2 Pass\n',64,64,True)[0]['bytes'],64)
        with self.assertRaises(ValueError): parse_output(HEADER+'64 2 Fail\n',64,64,True)
    def test_timeout_even_with_row(self):
        with self.assertRaises(ValueError): parse_output(HEADER+'64 2\nUCT send timeout\n',64,64)
    def test_environment_separation(self):
        with patch.dict(os.environ,{'UCX_RNDV_THRESH':'inf','LD_PRELOAD':'bad','OMPI_MCA_pml':'ob1'}):
            rm=environment(Path('/runtime'),'rm'); rc=environment(Path('/runtime'),'rc')
        self.assertNotIn('UCX_RNDV_THRESH',rm)
        self.assertNotIn('LD_PRELOAD',rm)
        self.assertNotIn('OMPI_MCA_pml',rm)
        self.assertFalse(any(k.startswith('UCX_RM_') for k in rc))
        self.assertFalse(any(k.startswith('UCX_RC_') for k in rm))
    def test_alternating_order_and_binding(self):
        a=argparse.Namespace(runtime='/runtime',install='/install',phase='gate',repeats=2,
                             ppn=1,binding='core',timeout=180,warmup=20,iterations=100,
                             window=64,validate=False)
        jobs=plan(a)
        self.assertEqual([j['transport'] for j in jobs],['rm','rc','rc','rm'])
        self.assertIn('pml_ucx_tls',jobs[0]['command'])
        self.assertEqual(jobs[0]['command'][jobs[0]['command'].index('--bind-to')+1],'core')
        self.assertNotIn('-c',jobs[0]['command'])
if __name__=='__main__': unittest.main()
