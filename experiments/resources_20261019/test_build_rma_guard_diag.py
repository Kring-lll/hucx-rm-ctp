import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import build_rma_guard_diag as builder

class PlanSafety(unittest.TestCase):
    def setup_tree(self, root):
        base=root/'baseline'; payload=root/'payload'; stage=root/'stage'; stage.mkdir()
        name='src/example.c'; text=b'int example;\n'
        for path in (base/'source'/name,payload/'overlay'/name):
            path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(text)
        (base/'source/configure').write_text('#!/bin/sh\n')
        hashes={name:hashlib.sha256(text).hexdigest()}
        (payload/'OVERLAY_SHA256.json').write_text(json.dumps(hashes))
        return base,payload,stage,hashes
    def invoke(self, base,payload,stage,hashes):
        with patch.object(builder,'__file__',str(payload/'builder.py')), \
             patch.object(builder,'BASELINE',hashes), \
             patch('sys.argv',['builder','--baseline-run',str(base),'--stage',str(stage)]), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            builder.main()
        return output.getvalue()
    def test_plan_does_not_create_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            b,p,s,h=self.setup_tree(Path(tmp))
            self.assertIn('PLAN_ONLY=1',self.invoke(b,p,s,h))
            self.assertEqual(list(s.iterdir()),[])
    def test_bad_baseline_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            b,p,s,h=self.setup_tree(Path(tmp)); (b/'source/src/example.c').write_text('wrong')
            with self.assertRaisesRegex(RuntimeError,'baseline source mismatch'): self.invoke(b,p,s,h)
            self.assertEqual(list(s.iterdir()),[])
    def test_bad_overlay_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            b,p,s,h=self.setup_tree(Path(tmp)); (p/'overlay/src/example.c').write_text('wrong')
            with self.assertRaisesRegex(RuntimeError,'overlay mismatch'): self.invoke(b,p,s,h)
            self.assertEqual(list(s.iterdir()),[])
if __name__=='__main__': unittest.main()
