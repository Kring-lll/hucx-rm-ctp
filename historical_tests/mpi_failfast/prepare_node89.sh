#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node89 || { echo 'STOP: run on node89'; exit 1; }
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
(cd "$bundle" && sha256sum -c SHA256SUMS)
stage=/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016
original="$stage/source_Cqh0sQ/hmpi-main"
old_mpi="$stage/run_e5R0dm/install"
hucx=/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install
binary="$stage/tests/rm_mpi_paths_20261016_v1"
test -f "$original/configure"
test ! -e "$bundle/runtime.json" || { echo 'STOP: runtime.json already exists; use that completed runtime'; exit 1; }
bash "$bundle/check_original.sh"
ssh root@9.88.43.97 'bash -s' < "$bundle/check_original.sh"
run=$(mktemp -d "$stage/mpi_failfast_run_XXXXXX")
printf 'RUN=%s\nIndependent MPI build; original MPI/HUCX installs stay in place.\n' "$run"
current_log=''
trap 'rc=$?; printf "PREPARE_FAILED=%s RUN=%s LOG=%s\n" "$rc" "$run" "$current_log"; if [ -f "$current_log" ]; then tail -n 60 "$current_log"; fi; exit "$rc"' ERR
sha256sum "$original/ompi/mca/pml/ucx/pml_ucx.c" \
  "$old_mpi/lib/openmpi/mca_pml_ucx.so" "$old_mpi/lib/libmpi.so.40" \
  "$hucx/lib/ucx/libuct_ub.so.0.0.0" "$hucx/lib/libucp.so.0.0.0" "$binary" \
  > "$run/ORIGINAL_SHA256SUMS"
cp -a "$original" "$run/source"
python3 "$bundle/patch_source.py" "$run/source"
mkdir "$run/build"
export LD_LIBRARY_PATH="$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
current_log="$run/configure.log"
printf 'CONFIGURING LOG=%s\n' "$current_log"
(
  cd "$run/build"
  CC=gcc CXX=g++ "$run/source/configure" \
    --prefix="$run/install" --with-ucx="$hucx" --without-ucg \
    --disable-mpi-fortran --disable-oshmem --with-pic
) > "$current_log" 2>&1
printf 'CONFIGURE_OK\n'
current_log="$run/build.log"
printf 'BUILDING -j4 LOG=%s\n' "$current_log"
make -C "$run/build" -j4 > "$current_log" 2>&1
printf 'BUILD_OK\n'
current_log="$run/install.log"
make -C "$run/build" install > "$current_log" 2>&1
printf 'INSTALL_OK\n'
mpi="$run/install"
export OPAL_PREFIX="$mpi"
export LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$mpi/bin/mpirun" --version
"$mpi/bin/ompi_info" --param pml ucx --level 1
ldd "$binary" > "$run/test_binary_ldd.txt"
grep -F "$mpi/lib/libmpi" "$run/test_binary_ldd.txt"
if grep -F 'not found' "$run/test_binary_ldd.txt"; then exit 1; fi
strings "$mpi/lib/openmpi/mca_pml_ucx.so" | grep -F HMPI_RM_FAIL_FAST_ARMED
sha256sum -c "$run/ORIGINAL_SHA256SUMS"
tar -C "$run" -czf "$run/runtime.tar.gz" install
(cd "$run" && sha256sum runtime.tar.gz > runtime.tar.gz.sha256)
ssh root@9.88.43.97 "test ! -e '$run' && mkdir '$run'"
scp "$run/runtime.tar.gz" "$run/runtime.tar.gz.sha256" "root@9.88.43.97:$run/"
ssh root@9.88.43.97 "bash -s -- '$run'" < "$bundle/install_remote.sh"
ssh root@9.88.43.97 "sha256sum '$mpi/lib/openmpi/mca_pml_ucx.so' '$mpi/lib/libmpi.so.40'" \
  > "$run/REMOTE_MPI_SHA256SUMS"
sha256sum -c "$run/REMOTE_MPI_SHA256SUMS"
bash "$bundle/check_original.sh"
ssh root@9.88.43.97 'bash -s' < "$bundle/check_original.sh"
python3 - "$bundle/runtime.json" "$run" <<'PY'
import json, sys
from pathlib import Path
state, run = map(Path, sys.argv[1:])
with state.open('x') as stream:
    json.dump({'version': 1, 'run': str(run), 'mpi_prefix': str(run / 'install'),
               'policy': 'opt_in_deferred_abort_job', 'switch': 'HUCX_MPI_RM_FAIL_FAST=1'},
              stream, indent=2)
    stream.write('\n')
PY
printf 'BOTH_NODES_FAILFAST_PREPARE_OK\nMPI_PREFIX=%s\nSTATE=%s/runtime.json\nDEFAULT_SWITCH=off\n' "$mpi" "$bundle"
