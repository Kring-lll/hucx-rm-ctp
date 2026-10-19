#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 BUILD_DIR URMA_INCLUDE_DIR" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/../.." && pwd)
build=$(realpath "$1")
sdk=$(realpath "$2")
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
${CC:-gcc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable \
    -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DHAVE_CONFIG_H -I"$build" -I"$build/src" -I"$root/src" -I"$sdk" \
    "$root/contrib/rm_ctp/test_ucp.c" \
    -Wl,--export-dynamic -L"$build/src/uct/urma/.libs" -luct_ub \
    -L"$build/src/ucp/.libs" -lucp -L"$build/src/uct/.libs" -luct \
    -L"$build/src/ucs/.libs" -lucs -lpthread -ldl -lm -o "$out/test_ucp"
env -u LD_PRELOAD LD_LIBRARY_PATH="$build/src/uct/urma/.libs:$build/src/ucp/.libs:$build/src/uct/.libs:$build/src/ucs/.libs:${LD_LIBRARY_PATH-}" \
    UCX_LOG_LEVEL=info ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" "$out/test_ucp"
