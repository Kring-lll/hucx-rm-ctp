#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node89 || { echo 'STOP: run on node89'; exit 1; }
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
stage=/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016
mpi="$stage/run_e5R0dm/install"
hucx=/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install
name=rm_mpi_timing_20261016_v1
(cd "$bundle" && sha256sum -c SHA256SUMS)
python3 -c 'import time; print("LOCAL_MONOTONIC_NS="+str(time.clock_gettime_ns(time.CLOCK_MONOTONIC)))'
bash "$bundle/check_runtime.sh"
ssh root@9.88.43.97 'bash -s' < "$bundle/check_runtime.sh"
test ! -e "$stage/tests/$name" || { echo 'STOP: timing binary exists'; exit 1; }
ssh root@9.88.43.97 "test ! -e '$stage/tests/$name'"
export OPAL_PREFIX="$mpi"
export LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
build=$(mktemp -d "$stage/timing_build_XXXXXX")
"$mpi/bin/mpicc" -std=c99 -O2 -g -Wall -Wextra -Werror \
    "$bundle/mpi_timing.c" -o "$build/$name"
cp "$build/$name" "$stage/tests/$name"
scp "$stage/tests/$name" "root@9.88.43.97:$stage/tests/"
hash=$(sha256sum "$stage/tests/$name" | awk '{print $1}')
printf '\nLOCAL_TIMING_BINARY_SHA256=%s\n' "$hash"
printf '%s  %s\n' "$hash" "$stage/tests/$name" \
    | ssh root@9.88.43.97 'sha256sum -c -'
printf 'MPI_TIMING_PREPARE_OK\n'
