#!/usr/bin/env python3
"""Independent diagnostic build on top of accepted rkey fix installation source."""
import argparse, hashlib, json, os, shutil, subprocess, tempfile
from pathlib import Path

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    a=argparse.ArgumentParser(); a.add_argument('--baseline-run',required=True); a.add_argument('--stage',required=True); a.add_argument('--execute',action='store_true'); x=a.parse_args()
    payload=Path(__file__).resolve().parent; baseline=Path(x.baseline_run).resolve(); stage=Path(x.stage).resolve()
    original=json.loads((payload/'BASELINE_SHA256.json').read_text()); overlay=json.loads((payload/'OVERLAY_SHA256.json').read_text())
    for n,h in original.items():
        assert sha(baseline/'source'/n)==h, 'baseline mismatch: '+n
    for n,h in overlay.items():
        assert sha(payload/'overlay'/n)==h, 'overlay mismatch: '+n
    print('COST_DIAG_SOURCE_CHECK=PASS',flush=True)
    if not x.execute: print('PLAN_ONLY=1'); return
    assert os.uname().nodename.split('.')[0]=='node89'
    out=Path(tempfile.mkdtemp(prefix='rm_cost_diag_build_',dir=stage)); print('COST_DIAG_BUILD_DIR='+str(out),flush=True)
    shutil.copytree(baseline/'source',out/'source',symlinks=False)
    for n in overlay: shutil.copy2(payload/'overlay'/n,out/'source'/n)
    (out/'build').mkdir()
    env={k:v for k,v in os.environ.items() if not k.startswith(('UCX_','OMPI_MCA_','RM_RESOURCE_','RM_SEG_')) and k not in ('LD_PRELOAD','LD_LIBRARY_PATH','CONFIG_SITE','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','LIBS','CC','CXX')}
    configure=[str(out/'source/configure'),'--prefix='+str(out/'install'),'--with-ub=/usr','--without-java','--without-cuda','--without-rocm','--without-verbs','--without-rdmacm','CC=gcc','CFLAGS=-O2 -g','CXX=g++','CXXFLAGS=-O2 -g']
    (out/'BUILD_MANIFEST.json').write_text(json.dumps({'baseline':str(baseline),'baseline_sha256':original,'overlay_sha256':overlay,'configure':configure,'source_commit':(payload/'SOURCE_COMMIT.txt').read_text().strip()},indent=2)+'\n')
    for label,command in [('configure',configure),('make',['make','-j4']),('install',['make','install'])]:
        log=out/(label+'.log'); print('STEP='+label+' LOG='+str(log),flush=True)
        with log.open('w') as f: r=subprocess.run(command,cwd=out/'build',env=env,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode: print('\n'.join(log.read_text(errors='replace').splitlines()[-60:])); raise SystemExit(r.returncode)
    for n,h in original.items(): assert sha(baseline/'source'/n)==h
    module=out/'install/lib/ucx/libuct_ub.so.0.0.0'; assert b'RM_COST_V1' in module.read_bytes()
    r=subprocess.run(['ldd',str(module)],env=dict(env,LD_LIBRARY_PATH=str(out/'install/lib')),capture_output=True,text=True)
    print(r.stdout); assert r.returncode==0 and 'not found' not in r.stdout+r.stderr
    print('COST_DIAG_MODULE_SHA256='+sha(module)); print('COST_DIAG_BUILD_OK='+str(out)); print('REAL_MPI_COST_DIAG=NOT_RUN')
if __name__=='__main__': main()
