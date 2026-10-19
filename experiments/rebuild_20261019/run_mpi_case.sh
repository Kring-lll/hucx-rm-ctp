#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node89 || { echo 'STOP: run on node89'; exit 1; }
if (( $# < 3 || $# > 4 )); then
    echo 'Usage: bash run_case.sh p2p|bcast|gather BYTES send|read|write [ITERS=1]'
    exit 1
fi
mode=$1
bytes=$2
path=$3
iters=${4:-1}
[[ "$bytes" =~ ^[1-9][0-9]{0,7}$ ]] && (( bytes <= 8388608 )) || exit 1
[[ "$iters" =~ ^([1-9]|10)$ ]] || exit 1
case "$mode" in
    p2p) ranks=2; hosts=9.88.43.89:1,9.88.43.97:1; tls=rm_urma,self ;;
    bcast|gather) ranks=3; hosts=9.88.43.89:1,9.88.43.97:2; tls=rm_urma,sm,self ;;
    *) echo 'STOP: invalid mode'; exit 1 ;;
esac
case "$path" in
    send) native=n; threshold=inf; scheme=auto; expected=SEND ;;
    read) native=y; threshold=8192; scheme=get_zcopy; expected=READ ;;
    write) native=y; threshold=8192; scheme=put_zcopy; expected=WRITE ;;
    *) echo 'STOP: invalid path'; exit 1 ;;
esac
if [[ "$path" != send ]] && (( bytes < 8192 )); then
    echo 'STOP: READ/WRITE test requires BYTES >= 8192'; exit 1
fi
stage=/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_repo_rebuild_20261019
mpi="$stage/mpi_run_15c7293/install"
hucx="$stage/hucx_run_15c7293/install"
binary="$stage/tests/rm_mpi_paths"
test -x "$binary"
export OPAL_PREFIX="$mpi"
export PATH="$mpi/bin:$hucx/bin:$PATH"
export LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
log=$(mktemp "$stage/mpi_${mode}_${path}_${bytes}_XXXXXX.log")
printf 'LOG=%s\nMODE=%s BYTES=%s PATH_REQUEST=%s ITERS=%s RANKS=%s\n' \
       "$log" "$mode" "$bytes" "$path" "$iters" "$ranks"
{
    printf 'TEST_CONFIG mode=%s bytes=%s requested=%s scheme=%s threshold=%s ranks=%s iterations=%s\n' \
        "$mode" "$bytes" "$path" "$scheme" "$threshold" "$ranks" "$iters"
    sha256sum "$binary" "$hucx/lib/ucx/libuct_ub.so.0.0.0" "$hucx/lib/libucp.so.0.0.0"
} >> "$log"
if timeout -k 10s 180s "$mpi/bin/mpirun" \
    --allow-run-as-root --prefix "$mpi" --host "$hosts" -np "$ranks" \
    --map-by slot --bind-to none \
    --mca pml ucx --mca pml_ucx_tls any --mca pml_ucx_devices any \
    -x PATH -x LD_LIBRARY_PATH -x OPAL_PREFIX \
    -x "UCX_MODULE_DIR=$hucx/lib/ucx" -x "UCX_TLS=$tls" \
    -x UCX_NET_DEVICES=udmac0d1e2:1 -x UCX_RM_URMA_PRIORITY=1 \
    -x UCX_PROTO_ENABLE=n -x UCX_RM_URMA_RX_DEPTH=1024 \
    -x UCX_RM_URMA_TIMEOUT=100ms -x UCX_RM_URMA_RETRIES=8 \
    -x "UCX_RM_URMA_NATIVE_RMA=$native" -x "UCX_RNDV_THRESH=$threshold" \
    -x "UCX_RNDV_SCHEME=$scheme" \
    -x UCX_RM_URMA_DIAG=n -x UCX_RM_URMA_TRACE=y \
    -x UCX_RM_URMA_TEST_DROP_DATA=0 -x UCX_RM_URMA_TEST_DROP_ACK=0 \
    -x UCX_LOG_LEVEL=info \
    "$binary" "$mode" "$bytes" "$iters" >> "$log" 2>&1; then
    result=0
else
    result=$?
fi
printf 'MPI_EXIT_CODE=%s\n' "$result" >> "$log"
grep -aE 'TEST_CONFIG|MPI_PATH_SETUP|PASS MPI|FAIL|WARN|ERROR|inter-node cfg|RM_CTP_CQE.*status=[1-9]|MPI_EXIT_CODE' \
    "$log" | tail -n 65 || true
awk '/RM_CTP_POST / {
    if ($0 ~ /opcode=SEND([[:space:]]|$)/) sends++
    if ($0 ~ /opcode=READ([[:space:]]|$)/) reads++
    if ($0 ~ /opcode=WRITE([[:space:]]|$)/) writes++
}
END {printf "POST_COUNTS SEND=%d READ=%d WRITE=%d\n", sends, reads, writes}' "$log"
awk '/RM_CTP_POST / && /opcode=(READ|WRITE)([[:space:]]|$)/ {print; if (++n==6) exit}' "$log"
if (( result != 0 )); then
    printf '\nFAILURE_LOG_TAIL:\n'; tail -n 40 "$log"
    printf 'LOG=%s\n' "$log"; exit "$result"
fi
passes=$(grep -ac '^PASS MPI ' "$log" || true)
if (( passes != ranks )); then
    printf 'PATH_CHECK=FAIL reason=missing_rank_content_checks\n' | tee -a "$log"
    exit 2
fi
if ! grep -aEq "RM_CTP_POST .*opcode=$expected([[:space:]]|$)" "$log"; then
    printf 'PATH_CHECK=FAIL reason=requested_opcode_not_observed expected=%s\n' "$expected" | tee -a "$log"
    printf 'LOG=%s\n' "$log"; exit 2
fi
printf 'PATH_CHECK=PASS observed=%s content_checks=%s\n' "$expected" "$passes" | tee -a "$log"
printf 'LOG=%s\n' "$log"
