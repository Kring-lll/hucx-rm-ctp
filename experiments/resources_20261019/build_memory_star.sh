#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
  echo 'Usage: build_memory_star.sh MPI_PREFIX NEW_ABSOLUTE_OUTPUT_DIR' >&2; exit 2
fi
mpi=$1
out=$2
[[ "$mpi" = /* && "$out" = /* ]] || exit 2
test -x "$mpi/bin/mpicc"
test ! -e "$out" && test ! -L "$out"
mkdir "$out"
src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
"$mpi/bin/mpicc" -std=gnu11 -O2 -g -Wall -Wextra -Werror \
  "$src/mpi_memory_star.c" -ldl -o "$out/mpi_memory_star"
ldd "$out/mpi_memory_star" > "$out/ldd.txt"
cat "$out/ldd.txt"
if grep -q 'not found' "$out/ldd.txt"; then exit 1; fi
(cd "$out"; sha256sum mpi_memory_star > SHA256SUMS; cat SHA256SUMS)
echo "MEMORY_STAR_BUILD_OK=$out"
echo 'NODE_MPI_MEMORY_ACCEPTANCE=NOT_RUN'
