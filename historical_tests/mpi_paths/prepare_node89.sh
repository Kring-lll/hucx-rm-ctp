#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node89 || { echo 'STOP: run on node89'; exit 1; }
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
stage=/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016
mpi="$stage/run_e5R0dm/install"
hucx=/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install
name=rm_mpi_paths_20261016_v1
cd "$bundle"
sha256sum -c SHA256SUMS
test ! -e "$stage/tests/$name" || { echo 'STOP: test binary exists'; exit 1; }
export OPAL_PREFIX="$mpi"
export LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$stage/tests"
build_dir=$(mktemp -d "$stage/path_test_build_XXXXXX")
"$mpi/bin/mpicc" -std=c99 -O2 -g -Wall -Wextra -Werror \
    "$bundle/mpi_paths.c" -o "$build_dir/$name"
ssh root@9.88.43.97 "mkdir -p '$stage/tests' && test ! -e '$stage/tests/$name'"
cp "$build_dir/$name" "$stage/tests/$name"
scp "$stage/tests/$name" "root@9.88.43.97:$stage/tests/"
printf '\nLOCAL_BINARY:\n'
sha256sum "$stage/tests/$name"
printf '\nREMOTE_BINARY:\n'
ssh root@9.88.43.97 "sha256sum '$stage/tests/$name'"
printf 'MPI_PATH_TEST_PREPARE_OK\n'
