#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node89 || { echo 'STOP: run on node89'; exit 1; }
if (( $# < 1 || $# > 4 )); then
  echo 'Usage: run_case.sh baseline|data1|ack1|exhaust [BYTES=64] [ITERS=1] [auto|read|write]'; exit 1
fi
case_name=$1
bytes=${2:-64}
iters=${3:-1}
path=${4:-auto}
case "$case_name" in baseline|data1|ack1|exhaust) ;; *) exit 1 ;; esac
[[ "$bytes" =~ ^[1-9][0-9]{0,7}$ ]] && (( bytes <= 8388608 )) || exit 1
[[ "$iters" =~ ^([1-9]|10)$ ]] || exit 1
scheme=auto
threshold=auto
case "$path" in
  auto) ;;
  read) (( bytes >= 8192 )) || exit 1; scheme=get_zcopy; threshold=8192 ;;
  write) (( bytes >= 8192 )) || exit 1; scheme=put_zcopy; threshold=8192 ;;
  *) exit 1 ;;
esac
if [[ "$case_name" != baseline && ( "$bytes" != 64 || "$iters" != 1 || "$path" != auto ) ]]; then
  echo 'STOP: fault cases are initially restricted to p2p64/1/auto'; exit 1
fi
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
(cd "$bundle" && sha256sum -c SHA256SUMS >/dev/null)
stage=/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016
mpi=$(python3 - "$bundle/runtime.json" <<'PY'
import json, re, sys
with open(sys.argv[1]) as stream:
    state = json.load(stream)
prefix = state['mpi_prefix']
if state.get('version') != 1 or not re.fullmatch(
    r'/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016/mpi_failfast_run_[A-Za-z0-9]+/install', prefix):
    raise SystemExit('STOP: unexpected runtime state')
print(prefix)
PY
)
hucx=/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install
binary="$stage/tests/rm_mpi_paths_20261016_v1"
bash "$bundle/check_original.sh" > /dev/null
export OPAL_PREFIX="$mpi"
export PATH="$mpi/bin:$hucx/bin:$PATH"
export LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
test -x "$mpi/bin/mpirun"
log=$(mktemp "$stage/mpi_failfast_${case_name}_${bytes}_XXXXXX.log")
printf 'LOG=%s\nCASE=%s BYTES=%s ITERS=%s PATH=%s POLICY=abort_job\n' \
  "$log" "$case_name" "$bytes" "$iters" "$path"
{
  printf 'TEST_CONFIG case=%s bytes=%s iterations=%s path=%s ranks=2 fail_fast=1\n' \
    "$case_name" "$bytes" "$iters" "$path"
  printf 'MPI_PREFIX=%s\n' "$mpi"
  sha256sum "$mpi/lib/openmpi/mca_pml_ucx.so" "$mpi/lib/libmpi.so.40" "$binary"
} >> "$log"
started=$SECONDS
if timeout -k 10s 180s "$mpi/bin/mpirun" \
    --allow-run-as-root --prefix "$mpi" \
    --host 9.88.43.89:1,9.88.43.97:1 -np 2 --map-by slot --bind-to none \
    --mca pml ucx --mca pml_ucx_tls any --mca pml_ucx_devices any \
    -x PATH -x LD_LIBRARY_PATH -x OPAL_PREFIX \
    -x "UCX_MODULE_DIR=$hucx/lib/ucx" -x UCX_TLS=rm_urma,self \
    -x UCX_NET_DEVICES=udmac0d1e2:1 -x UCX_RM_URMA_PRIORITY=1 \
    -x UCX_PROTO_ENABLE=n -x UCX_RM_URMA_RX_DEPTH=1024 \
    -x UCX_RM_URMA_TIMEOUT=100ms -x UCX_RM_URMA_RETRIES=8 \
    -x UCX_RM_URMA_NATIVE_RMA=y -x "UCX_RNDV_THRESH=$threshold" \
    -x "UCX_RNDV_SCHEME=$scheme" -x UCX_RM_URMA_DIAG=n \
    -x UCX_RM_URMA_TRACE=y -x UCX_LOG_LEVEL=info \
    -x HUCX_MPI_RM_FAIL_FAST=1 \
    -x UCX_RM_URMA_TEST_DROP_DATA=0 -x UCX_RM_URMA_TEST_DROP_ACK=0 \
    /bin/bash -c '
      node=$(hostname -s)
      case "$node" in node89|node97) ;; *) exit 2 ;; esac
      case "$2:$node" in
        data1:node97) export UCX_RM_URMA_TEST_DROP_DATA=1 ;;
        ack1:node89) export UCX_RM_URMA_TEST_DROP_ACK=1 ;;
        exhaust:node97) export UCX_RM_URMA_TEST_DROP_DATA=64 ;;
      esac
      printf "FAULT_RANK_CONFIG node=%s pid=%s DATA=%s ACK=%s\n" \
        "$node" "$$" "$UCX_RM_URMA_TEST_DROP_DATA" "$UCX_RM_URMA_TEST_DROP_ACK"
      exec "$1" p2p "$3" "$4"
    ' rm-failfast-rank "$binary" "$case_name" "$bytes" "$iters" >> "$log" 2>&1; then
  result=0
else
  result=$?
fi
printf 'MPI_EXIT_CODE=%s\nJOB_WALL_SECONDS_APPROX=%s\n' "$result" "$((SECONDS-started))" >> "$log"
if python3 "$bundle/summarize.py" "$log" "$case_name" "$bytes" "$iters" "$path" "$result"; then
  check=0
else
  check=$?
fi
if (( check != 0 )); then printf '\nLOG_TAIL:\n'; tail -n 40 "$log"; fi
printf 'LOG=%s\n' "$log"
# On an expected-negative case, return 0 only when the negative evidence passes.
# The real mpirun exit code remains explicitly recorded in the log/summary.
exit "$check"
