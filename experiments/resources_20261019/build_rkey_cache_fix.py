#!/usr/bin/env python3
"""Copy the pinned HUCX source and build a rkey cache fix candidate in an independent install."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

BASELINE = {
    'src/uct/urma/base/urma_md.h': 'cc2cf92b0bb88e024d5f670752496e220b8af5c79a3d14b1b082543338aa479f',
    'src/uct/urma/rm_urma/rm_urma_ep.c': '4d94f417acec4bc7eb135f403f3e9b9a912d061f45f9ea38d6cc7e26eee9f5da',
    'src/uct/urma/base/urma_md.c': 'f34313d4630cb23073389b5f98372a4491bb86c6666f291af7480c8aa4bb6b31',
}
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-run',required=True)
    parser.add_argument('--stage',required=True)
    parser.add_argument('--execute',action='store_true')
    args=parser.parse_args()
    baseline=Path(args.baseline_run).resolve()
    stage=Path(args.stage).resolve()
    payload=Path(__file__).resolve().parent
    assert stage.is_dir(), stage
    manifest=json.loads((payload/'OVERLAY_SHA256.json').read_text())
    assert set(manifest)==set(BASELINE)
    for name, expected in BASELINE.items():
        if digest(baseline/'source'/name)!=expected:
            raise RuntimeError('baseline source mismatch: '+name)
        if digest(payload/'overlay'/name)!=manifest[name]:
            raise RuntimeError('diagnostic overlay mismatch: '+name)
    assert (baseline/'source/configure').is_file()
    print('SOURCE_AND_OVERLAY_CHECK=PASS',flush=True)
    if not args.execute:
        print('PLAN_ONLY=1 no_build_no_MPI=1'); return
    if os.uname().nodename.split('.')[0]!='node89':
        raise RuntimeError('build only on node89')
    out=Path(tempfile.mkdtemp(prefix='rkey_cache_fix_build_',dir=stage))
    print('FIX_BUILD_DIR='+str(out),flush=True)
    shutil.copytree(baseline/'source',out/'source',symlinks=False)
    for name, expected in BASELINE.items():
        if digest(out/'source'/name)!=expected:
            raise RuntimeError('copied baseline mismatch: '+name)
        shutil.copy2(payload/'overlay'/name,out/'source'/name)
    (out/'build').mkdir()
    configure=[str(out/'source/configure'),'--prefix='+str(out/'install'),
               '--with-ub=/usr','--without-java','--without-cuda','--without-rocm',
               '--without-verbs','--without-rdmacm','CC=gcc','CFLAGS=-O2 -g',
               'CXX=g++','CXXFLAGS=-O2 -g']
    env={k:v for k,v in os.environ.items()
         if not k.startswith(('UCX_','OMPI_MCA_'))
         and k not in ('LD_PRELOAD','LD_LIBRARY_PATH','RM_RESOURCE_OBSERVER_DIR',
                      'RM_SEG_OBSERVER_DIR','CONFIG_SITE','CFLAGS','CXXFLAGS',
                      'CPPFLAGS','LDFLAGS','LIBS','CC','CXX')}
    record={'baseline_run':str(baseline),'baseline_sha256':BASELINE,
            'overlay_sha256':manifest,'configure':configure,
            'purpose':'rkey cache fix candidate; node MPI acceptance pending'}
    (out/'BUILD_MANIFEST.json').write_text(json.dumps(record,indent=2)+'\n')
    for label,command in [('lifecycle_selftest',[sys.executable,str(payload/'test_ub_rkey_lifecycle.py'),'--source-root',str(out/'source')]),('configure',configure),('make',['make','-j4']),
                          ('install',['make','install'])]:
        logpath=out/(label+'.log')
        print('STEP='+label+' LOG='+str(logpath),flush=True)
        with logpath.open('w') as log:
            result=subprocess.run(command,cwd=out/'build',env=env,
                                  stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:
            print('\n'.join(logpath.read_text(errors='replace').splitlines()[-80:]))
            raise SystemExit(result.returncode)
    for name,expected in BASELINE.items():
        if digest(baseline/'source'/name)!=expected:
            raise RuntimeError('original source changed during build')
    module=out/'install/lib/ucx/libuct_ub.so.0.0.0'
    if not module.is_file(): raise RuntimeError('missing URMA module')
    for symbol in ('RM_RMA_REJECT','UB_RKEY_CACHE_MISMATCH'):
        if symbol.encode() not in module.read_bytes():
            raise RuntimeError('missing diagnostic marker: '+symbol)
    libenv=dict(env,LD_LIBRARY_PATH=str(out/'install/lib'))
    check=subprocess.run(['ldd',str(module)],env=libenv,capture_output=True,text=True)
    (out/'ldd.log').write_text(check.stdout+check.stderr)
    print(check.stdout,flush=True)
    if check.returncode or 'not found' in check.stdout+check.stderr:
        raise RuntimeError('module dependencies unresolved')
    (out/'MODULE_SHA256SUMS').write_text(digest(module)+'  '+str(module)+'\n')
    print('FIX_MODULE_SHA256='+digest(module),flush=True)
    print('RKEY_CACHE_FIX_BUILD_OK='+str(out),flush=True)
    print('REAL_MPI_FIX_ACCEPTANCE=NOT_RUN',flush=True)
if __name__=='__main__': main()
