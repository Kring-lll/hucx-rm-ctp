#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node89 || { echo 'STOP: run on node89'; exit 1; }
if (( $# < 2 || $# > 3 )); then
    echo 'Usage: bash run_auto.sh p2p|bcast|gather BYTES [ITERS=1]'; exit 1
fi
mode=$1
bytes=$2
iters=${3:-1}
[[ "$bytes" =~ ^[1-9][0-9]{0,7}$ ]] && (( bytes <= 8388608 )) || exit 1
[[ "$iters" =~ ^([1-9]|10)$ ]] || exit 1
case "$mode" in
    p2p) ranks=2; hosts=9.88.43.89:1,9.88.43.97:1; tls=rm_urma,self ;;
    bcast|gather) ranks=3; hosts=9.88.43.89:1,9.88.43.97:2; tls=rm_urma,sm,self ;;
    *) echo 'STOP: invalid mode'; exit 1 ;;
esac
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
stage=/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016
mpi="$stage/run_e5R0dm/install"
hucx=/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install
binary="$stage/tests/rm_mpi_paths_20261016_v1"
test -x "$binary"
command -v python3 >/dev/null
export OPAL_PREFIX="$mpi"
export PATH="$mpi/bin:$hucx/bin:$PATH"
export LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
log=$(mktemp "$stage/mpi_auto_${mode}_${bytes}_XXXXXX.log")
printf 'LOG=%s\nAUTO: native=y threshold=auto scheme=auto; RX=1024 timeout=100ms\n' "$log"
{
    printf 'TEST_CONFIG mode=%s bytes=%s native=y threshold=auto scheme=auto ranks=%s iterations=%s\n' \
        "$mode" "$bytes" "$ranks" "$iters"
    sha256sum "$binary" "$hucx/lib/ucx/libuct_ub.so.0.0.0" "$hucx/lib/libucp.so.0.0.0"
} >> "$log"
started=$SECONDS
if timeout -k 10s 180s "$mpi/bin/mpirun" \
    --allow-run-as-root --prefix "$mpi" --host "$hosts" -np "$ranks" \
    --map-by slot --bind-to none \
    --mca pml ucx --mca pml_ucx_tls any --mca pml_ucx_devices any \
    -x PATH -x LD_LIBRARY_PATH -x OPAL_PREFIX \
    -x "UCX_MODULE_DIR=$hucx/lib/ucx" -x "UCX_TLS=$tls" \
    -x UCX_NET_DEVICES=udmac0d1e2:1 -x UCX_RM_URMA_PRIORITY=1 \
    -x UCX_PROTO_ENABLE=n -x UCX_RM_URMA_RX_DEPTH=1024 \
    -x UCX_RM_URMA_TIMEOUT=100ms -x UCX_RM_URMA_RETRIES=8 \
    -x UCX_RM_URMA_NATIVE_RMA=y -x UCX_RNDV_THRESH=auto \
    -x UCX_RNDV_SCHEME=auto \
    -x UCX_RM_URMA_DIAG=n -x UCX_RM_URMA_TRACE=y \
    -x UCX_RM_URMA_TEST_DROP_DATA=0 -x UCX_RM_URMA_TEST_DROP_ACK=0 \
    -x UCX_LOG_LEVEL=info \
    "$binary" "$mode" "$bytes" "$iters" >> "$log" 2>&1; then
    result=0
else
    result=$?
fi
printf 'MPI_EXIT_CODE=%s\nJOB_WALL_SECONDS_APPROX=%s\n' "$result" "$((SECONDS-started))" >> "$log"
set +e
python3 "$bundle/summarize_auto.py" "$log" "$mode" "$bytes" "$ranks" "$iters" "$result"
check=$?
set -e
if (( result != 0 )); then
    printf '\nFAILURE_LOG_TAIL:\n'; tail -n 40 "$log"
fi
printf 'LOG=%s\n' "$log"
exit "$check"
