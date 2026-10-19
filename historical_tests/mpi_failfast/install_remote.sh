#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node97
run=$1
case "$run" in
  /home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016/mpi_failfast_run_*) ;;
  *) echo 'STOP: unexpected experiment directory'; exit 1 ;;
esac
test ! -e "$run/install"
(cd "$run" && sha256sum -c runtime.tar.gz.sha256)
tar -xzf "$run/runtime.tar.gz" -C "$run"
mpi="$run/install"
hucx=/home/y00993710/urma_rm_ctp_lab/experiments/hucx_rm_ctp_v1_20261008/install
binary=/home/y00993710/urma_rm_ctp_lab/experiments/hmpi_rm_verify_20261016/tests/rm_mpi_paths_20261016_v1
export OPAL_PREFIX="$mpi"
export LD_LIBRARY_PATH="$mpi/lib:$hucx/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$mpi/bin/mpirun" --version
"$mpi/bin/ompi_info" --param pml ucx --level 1
ldd "$binary" > "$run/test_binary_ldd.txt"
grep -F "$mpi/lib/libmpi" "$run/test_binary_ldd.txt"
if grep -F 'not found' "$run/test_binary_ldd.txt"; then exit 1; fi
strings "$mpi/lib/openmpi/mca_pml_ucx.so" | grep -F HMPI_RM_FAIL_FAST_ARMED
sha256sum "$mpi/lib/openmpi/mca_pml_ucx.so" "$mpi/lib/libmpi.so.40"
printf 'FAILFAST_REMOTE_INSTALL_OK\n'
