import json,subprocess,sys,unittest
from pathlib import Path
class Config(unittest.TestCase):
 def config(self,mode,pool="y"):
  f=Path(__file__).with_name("run_memory_pool_star.py")
  r=subprocess.run([sys.executable,str(f),mode,"2","--stage","/tmp/stage","--runtime","/tmp/runtime","--build-dir","/tmp/build","--binary-sha256","a"*64,"--ucx-install","/tmp/pool/install","--module-sha256","b"*64,"--tx-pool",pool,"--dry-run"],capture_output=True,text=True,check=True)
  return json.loads(r.stdout)
 def test_rm_pool(self):
  d=self.config("rm");self.assertEqual(d["ucx_env"]["UCX_RM_URMA_TX_POOL"],"y");self.assertEqual(d["app_ucx_preload"].count(":"),3)
 def test_rm_off(self):self.assertEqual(self.config("rm","n")["ucx_env"]["UCX_RM_URMA_TX_POOL"],"n")
 def test_rc(self):
  d=self.config("rc");self.assertNotIn("UCX_RM_URMA_TX_POOL",d["ucx_env"]);self.assertEqual(d["ucx_env"]["UCX_MODULE_DIR"],"/tmp/pool/install/lib/ucx")
if __name__=="__main__":unittest.main()
