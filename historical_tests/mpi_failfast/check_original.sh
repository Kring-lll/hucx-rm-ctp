#!/usr/bin/env bash
set -euo pipefail
stage=/home/y00993710/urma_rm_ctp_lab/experiments
hucx="$stage/hucx_rm_ctp_v1_20261008/install"
mpi="$stage/hmpi_rm_verify_20261016/run_e5R0dm/install"
binary="$stage/hmpi_rm_verify_20261016/tests/rm_mpi_paths_20261016_v1"
printf 'NODE=%s\n' "$(hostname -s)"
printf '%s  %s\n' \
    f4f74c9ea357a9f0943197ff7a6d8e75c5b8769bf6ac4c16912e390a967af12d "$hucx/lib/ucx/libuct_ub.so.0.0.0" \
    a7eb32ec5980b942ef4e2457dbb93dd33b6de7b820f9b6e675f5e7d6f30e1086 "$hucx/lib/libucp.so.0.0.0" \
    55a9a830cd6d5a7f2f58531de9b2c10c83d03afbd939583962d7fee8588afb7a "$binary" \
    | sha256sum -c -
OPAL_PREFIX="$mpi" LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$mpi/bin/mpirun" --version
