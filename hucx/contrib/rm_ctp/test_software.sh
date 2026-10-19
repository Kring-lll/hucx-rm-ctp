#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc=${CC:-gcc}
"$cc" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$root/src/uct/urma/rm_urma" \
    "$root/contrib/rm_ctp/test_reliable.c" "$root/src/uct/urma/rm_urma/rm_urma_reliable.c" \
    -o "$out/test_reliable"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" "$out/test_reliable"
