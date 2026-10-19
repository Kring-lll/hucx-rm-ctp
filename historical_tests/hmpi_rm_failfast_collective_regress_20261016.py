#!/usr/bin/env python3
"""Normal collective regression for the isolated 20261016 fail-fast MPI.

Run on node89 only:
  python3 THIS_FILE bcast 8388608 read 5
  python3 THIS_FILE gather 8388608 write 5
Reuses the installed MPI/test binary. No rebuild, patch, or fault injection.
This checks normal collectives, not collective failure recovery.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time
import os

STAGE = Path('/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016')
HUCX = Path('/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install')


def verify(text, mode, size, iterations, path, result):
    reasons = []
    passed = set()
    placements = {}
    counts = Counter()
    for match in re.finditer(
        r'PASS MPI mode=(\w+) rank=(\d+) ranks=(\d+) bytes=(\d+) iterations=(\d+)', text):
        observed_mode, rank, ranks, count, rounds = match.groups()
        if (observed_mode, int(ranks), int(count), int(rounds)) == (mode, 3, size, iterations):
            passed.add(int(rank))
    for match in re.finditer(
        r'MPI_PATH_SETUP mode=(\w+) rank=(\d+) ranks=3 node=([^\s]+)', text):
        if match.group(1) == mode:
            placements[int(match.group(2))] = match.group(3)
    for match in re.finditer(r'RM_CTP_POST id=\d+ kind=\d+ opcode=(SEND|READ|WRITE)\b', text):
        counts[match.group(1)] += 1
    if result != 0:
        reasons.append('MPI_nonzero_exit')
    if passed != {0, 1, 2}:
        reasons.append('missing_distinct_rank_content_checks')
    if placements != {0: 'node89', 1: 'node97', 2: 'node97'}:
        reasons.append('unexpected_process_placement')
    if not re.search(r'inter-node cfg[^\n]*tag\(rm_urma/udmac0d1e2:1\)', text):
        reasons.append('RM_inter_node_path_not_observed')
    if 'HMPI_RM_FAIL_FAST_ARMED' not in text:
        reasons.append('new_MPI_callback_registration_not_observed')
    if re.search(r'HMPI_RM_FAIL_FAST peer=|RM_CTP_TEST_DROP|FAIL MPI|FAIL DATA|'
                 r'no error callback is installed|UCT send timeout|'
                 r'Caught signal|Segmentation fault|AddressSanitizer', text):
        reasons.append('unexpected_failure_or_fault_injection')
    if re.search(r'RM_CTP_CQE[^\n]*status=[1-9]\d*\b', text):
        reasons.append('nonzero_CQE')
    if path == 'read' and (not counts['READ'] or counts['WRITE']):
        reasons.append('requested_READ_path_not_confirmed')
    if path == 'write' and (not counts['WRITE'] or counts['READ']):
        reasons.append('requested_WRITE_path_not_confirmed')
    return reasons, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('bcast', 'gather'))
    parser.add_argument('bytes', type=int)
    parser.add_argument('path', choices=('read', 'write'))
    parser.add_argument('iterations', type=int, nargs='?', default=5)
    args = parser.parse_args()
    if socket.gethostname().split('.')[0] != 'node89':
        raise SystemExit('STOP: run on node89')
    if not 8192 <= args.bytes <= 8388608 or not 1 <= args.iterations <= 10:
        raise SystemExit('STOP: bytes or iterations out of range')
    with (STAGE / 'upload/hmpi_rm_failfast_20261016_v1/runtime.json').open() as stream:
        state = json.load(stream)
    prefix = state['mpi_prefix']
    if state.get('version') != 1 or not re.fullmatch(
        re.escape(str(STAGE)) + r'/mpi_failfast_run_[A-Za-z0-9]+/install', prefix):
        raise SystemExit('STOP: unexpected runtime state')
    mpi = Path(prefix)
    binary = STAGE / 'tests/rm_mpi_paths_20261016_v1'
    identities = {
        mpi / 'lib/openmpi/mca_pml_ucx.so': '3694579101a32873be7eb1a92ce324f7b59b19a46c508253cca6e8377973042b',
        mpi / 'lib/libmpi.so.40': '2a20f14c9a56eb2fb97d0e32a04373fc78a574f3b96eb6032929869cd32fc824',
        HUCX / 'lib/ucx/libuct_ub.so.0.0.0': 'f4f74c9ea357a9f0943197ff7a6d8e75c5b8769bf6ac4c16912e390a967af12d',
        HUCX / 'lib/libucp.so.0.0.0': 'a7eb32ec5980b942ef4e2457dbb93dd33b6de7b820f9b6e675f5e7d6f30e1086',
        binary: '55a9a830cd6d5a7f2f58531de9b2c10c83d03afbd939583962d7fee8588afb7a',
    }
    for filename, expected in identities.items():
        if hashlib.sha256(filename.read_bytes()).hexdigest() != expected:
            raise SystemExit('STOP: unexpected binary hash: ' + str(filename))
    settings = {
        'OPAL_PREFIX': str(mpi),
        'PATH': str(mpi / 'bin') + ':' + str(HUCX / 'bin') + ':' + os.environ.get('PATH', ''),
        'LD_LIBRARY_PATH': str(mpi / 'lib') + ':' + str(HUCX / 'lib') +
                           (':' + os.environ['LD_LIBRARY_PATH'] if os.environ.get('LD_LIBRARY_PATH') else ''),
        'UCX_MODULE_DIR': str(HUCX / 'lib/ucx'),
        'UCX_TLS': 'rm_urma,sm,self',
        'UCX_NET_DEVICES': 'udmac0d1e2:1',
        'UCX_RM_URMA_PRIORITY': '1',
        'UCX_PROTO_ENABLE': 'n',
        'UCX_RM_URMA_RX_DEPTH': '1024',
        'UCX_RM_URMA_TIMEOUT': '100ms',
        'UCX_RM_URMA_RETRIES': '8',
        'UCX_RM_URMA_NATIVE_RMA': 'y',
        'UCX_RNDV_THRESH': '8192',
        'UCX_RNDV_SCHEME': 'get_zcopy' if args.path == 'read' else 'put_zcopy',
        'UCX_RM_URMA_DIAG': 'n',
        'UCX_RM_URMA_TRACE': 'y',
        'UCX_RM_URMA_TEST_DROP_DATA': '0',
        'UCX_RM_URMA_TEST_DROP_ACK': '0',
        'UCX_LOG_LEVEL': 'info',
        'HUCX_MPI_RM_FAIL_FAST': '1',
    }
    env = dict(os.environ)
    env.update(settings)
    command = ['timeout', '-k', '10s', '180s', str(mpi / 'bin/mpirun'),
               '--allow-run-as-root', '--prefix', str(mpi),
               '--host', '9.88.43.89:1,9.88.43.97:2', '-np', '3',
               '--map-by', 'slot', '--bind-to', 'none',
               '--mca', 'pml', 'ucx', '--mca', 'pml_ucx_tls', 'any',
               '--mca', 'pml_ucx_devices', 'any']
    for key in settings:
        command.extend(('-x', key))
    command.extend((str(binary), args.mode, str(args.bytes), str(args.iterations)))
    fd, name = tempfile.mkstemp(prefix=f'mpi_failfast_{args.mode}_{args.path}_{args.bytes}_',
                                suffix='.log', dir=STAGE)
    print('LOG=' + name, flush=True)
    print(f'MODE={args.mode} BYTES={args.bytes} ITERS={args.iterations} PATH={args.path} RANKS=3 FAIL_FAST=1', flush=True)
    with os.fdopen(fd, 'w') as stream:
        stream.write(f'MPI_PREFIX={mpi}\n')
        stream.write('TEST_CONFIG ' + json.dumps(settings, ensure_ascii=True) + '\n')
        stream.flush()
        start = time.monotonic()
        result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
        elapsed = time.monotonic() - start
        stream.write(f'MPI_EXIT_CODE={result}\nJOB_WALL_SECONDS={elapsed:.3f}\n')
    text = Path(name).read_text(errors='replace')
    reasons, counts = verify(text, args.mode, args.bytes, args.iterations, args.path, result)
    for line in text.splitlines():
        if any(word in line for word in ('MPI_PREFIX=', 'MPI_PATH_SETUP', 'HMPI_RM_FAIL_FAST',
                                         'inter-node cfg', 'PASS MPI', 'MPI_EXIT_CODE=', 'JOB_WALL_SECONDS=')):
            print(line)
    print('POST_COUNTS ' + ' '.join(f'{op}={counts[op]}' for op in ('SEND', 'READ', 'WRITE')))
    print('COLLECTIVE_REGRESSION=' + ('FAIL' if reasons else 'PASS'))
    for reason in reasons:
        print('CHECK_REASON=' + reason)
    if reasons:
        print('LOG_TAIL:')
        print('\n'.join(text.splitlines()[-30:]))
    print('LOG=' + name)
    raise SystemExit(1 if reasons else 0)


if __name__ == '__main__':
    main()
