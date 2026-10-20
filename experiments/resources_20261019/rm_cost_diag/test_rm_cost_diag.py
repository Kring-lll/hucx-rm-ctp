import unittest
from pathlib import Path
class Layout(unittest.TestCase):
    def setUp(self):
        p=Path(__file__).resolve().parent/'overlay/src/uct/urma/rm_urma'
        self.c=(p/'rm_urma_iface.c').read_text(); self.h=(p/'rm_urma_iface.h').read_text()
    def test_before_teardown(self):
        s=self.c[self.c.index('static void rm_cleanup('):]
        self.assertLess(s.index('rm_cost_summary(self)'),s.index('rm_stop_io(self)'))
    def test_separate_registration(self):
        self.assertIn('ucs_get_time() - cost_start - op->cost_reg',self.c)
        self.assertIn('cost_unreg_before',self.c)
    def test_kinds_and_markers(self):
        self.assertIn('k <= 3',self.c); self.assertIn('RM_COST_V1',self.c)
        self.assertIn('cost_cqe_count[4]',self.h)
if __name__=='__main__': unittest.main()
