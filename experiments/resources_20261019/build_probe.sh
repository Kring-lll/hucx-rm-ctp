#!/usr/bin/env bash
set -euo pipefail
if [ "$#" != 1 ]; then
    echo "usage: bash build_probe.sh NEW_ABSOLUTE_OUTPUT_DIRECTORY" >&2; exit 2
fi
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
output=$1
case "$output" in /*) ;; *) echo 'STOP: output must be absolute'; exit 2 ;; esac
test ! -e "$output"
test ! -L "$output"
sdk=${URMA_OBSERVER_INCLUDE:-/usr/include/ub/umdk/urma}
test -f "$sdk/urma_api.h"
mkdir "$output"
mkdir "$output/unit" "$output/selftest_logs"
printf 'OUTPUT=%s\n' "$output"
${CC:-gcc} -std=gnu11 -O2 -g -Wall -Wextra -Werror -fPIC -shared \
    -I"$sdk" "$source_dir/urma_resource_observer.c" \
    -Wl,-z,defs -ldl -pthread -o "$output/liburma_resource_observer.so"
${CC:-gcc} -std=gnu11 -O2 -g -Wall -Wextra -Werror -fPIC -shared \
    -I"$sdk" "$source_dir/mock_urma_backend.c" -o "$output/unit/liburma_observer_mock.so"
${CC:-gcc} -std=gnu11 -O2 -g -Wall -Wextra -Werror \
    -I"$sdk" "$source_dir/observer_selftest.c" \
    -L"$output/unit" -Wl,-rpath,"$output/unit" -lurma_observer_mock -ldl \
    -o "$output/unit/observer_selftest"
RM_RESOURCE_OBSERVER_DIR="$output/selftest_logs" \
LD_PRELOAD="$output/liburma_resource_observer.so" \
    "$output/unit/observer_selftest"
python3 "$source_dir/check_selftest.py" "$output/selftest_logs"
sha256sum "$output/liburma_resource_observer.so" > "$output/OBSERVER_SHA256SUMS"
printf 'OBSERVER_BUILD_AND_MOCK_SELFTEST_OK=%s\n' "$output"
printf 'REAL_MPI_INTERPOSITION_AND_RESOURCE_COMPARISON=NOT_RUN\n'
