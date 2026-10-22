#!/usr/bin/env bash
set -euo pipefail
(( $# == 1 )) || { echo 'Usage: build_segment_probe.sh NEW_ABSOLUTE_OUTPUT_DIR'; exit 2; }
out=$1
[[ "$out" = /* ]] || exit 2
test ! -e "$out" && test ! -L "$out"
src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
inc=${URMA_OBSERVER_INCLUDE:-/usr/include/ub/umdk/urma}
test -f "$inc/urma_api.h"
mkdir -p "$out/unit" "$out/mock_logs"
flags=(-std=gnu11 -O2 -g -Wall -Wextra -Werror)
gcc "${flags[@]}" -I"$inc" -fPIC -shared "$src/segment_observer.c" \
  -Wl,-z,defs -ldl -pthread -o "$out/liburma_segment_observer.so"
gcc "${flags[@]}" -I"$inc" -fPIC -shared "$src/segment_mock.c" \
  -Wl,-z,defs -o "$out/unit/libsegment_mock.so"
gcc "${flags[@]}" -I"$inc" "$src/segment_selftest.c" \
  -L"$out/unit" -Wl,-rpath,"$out/unit" -lsegment_mock -o "$out/unit/segment_selftest"
RM_SEG_OBSERVER_DIR="$out/mock_logs" LD_PRELOAD="$out/liburma_segment_observer.so" \
  "$out/unit/segment_selftest"
python3 "$src/check_segment_selftest.py" "$out/mock_logs"
(cd "$out"; sha256sum liburma_segment_observer.so > SHA256SUMS; cat SHA256SUMS)
echo "SEGMENT_BUILD_AND_MOCK_SELFTEST_OK=$out"
echo 'REAL_MPI_SEGMENT_OBSERVATION=NOT_RUN'
