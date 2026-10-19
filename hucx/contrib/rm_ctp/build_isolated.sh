#!/usr/bin/env bash
# Fresh source/build/install only. Never writes the existing node installation.
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "usage: $0 NEW_ABSOLUTE_STAGE_UNDER_LAB_EXPERIMENTS" >&2; exit 2; fi
source_root=$(cd "$(dirname "$0")/../.." && pwd -P)
lab=$(realpath -e "${HUCX_LAB:-/home/y00993710/urma_rm_ctp_lab}")
target=$(realpath -m "$1")
case "$target" in "$lab"/experiments/*) ;; *) echo "STOP: target must be under $lab/experiments" >&2; exit 2;; esac
case "$target" in "$source_root"|"$source_root"/*) echo 'STOP: target must be outside the source tree' >&2; exit 2;; esac
if [ -e "$target" ] || [ -L "$target" ]; then echo 'STOP: target already exists' >&2; exit 2; fi
for command_name in gcc g++ make autoconf automake libtoolize flex tar; do
    command -v "$command_name" >/dev/null || { echo "MISSING: $command_name" >&2; exit 2; }
done
jobs=${HUCX_JOBS:-4}
if [[ ! "$jobs" =~ ^[1-9][0-9]*$ ]] || [ "$jobs" -gt 64 ]; then echo 'STOP: HUCX_JOBS must be 1..64' >&2; exit 2; fi
mkdir -- "$target" # atomic refusal if another process created the directory
mkdir "$target/source" "$target/build"
trap 'code=$?; if [ "$code" -ne 0 ]; then printf "BUILD_FAILED exit=%s stage=%s\n" "$code" "$target" >&2; fi' EXIT
tar -C "$source_root" --exclude='./.git' --exclude='./autom4te.cache' -cf - . |
    tar -C "$target/source" -xf -
{
    date -u '+UTC=%Y-%m-%dT%H:%M:%SZ'
    uname -a
    gcc --version | head -n 1
    sha256sum "$target/source/contrib/rm_ctp/BASELINES.json"
    printf 'URMA_PREFIX=%s\n' "${HUCX_URMA_PREFIX:-/usr}"
} > "$target/build_environment.txt"
(cd "$target/source" && ./autogen.sh) > "$target/autogen.log" 2>&1
(cd "$target/build" && CC="${CC:-gcc}" CXX="${CXX:-g++}" \
    CFLAGS="${CFLAGS:--O2 -g}" CXXFLAGS="${CXXFLAGS:--O2 -g}" \
    ../source/configure --prefix="$target/install" \
    --with-ub="${HUCX_URMA_PREFIX:-/usr}" --without-java --without-cuda \
    --without-rocm --without-verbs --without-rdmacm) > "$target/configure.log" 2>&1
# The public UM source still includes verbs.h even with --without-verbs.
# It must be supplied by the installed SDK/build dependencies; never auto-install them.
grep -Eq '^module_LTLIBRARIES[[:space:]]*=[[:space:]]*libuct_ub\.la' \
    "$target/build/src/uct/urma/Makefile" || { echo 'STOP: URMA module not configured' >&2; exit 1; }
make -C "$target/build" -j"$jobs" > "$target/build.log" 2>&1
install_prefix=$(sed -n 's/^prefix = //p' "$target/build/Makefile")
if [ "$install_prefix" != "$target/install" ] || [ -e "$target/install" ] || [ -L "$target/install" ]; then
    echo 'STOP: installation prefix or existing target check failed' >&2; exit 1
fi
make -C "$target/build" install > "$target/install.log" 2>&1
${CC:-gcc} -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
    -I"$target/install/include" "$target/source/contrib/rm_ctp/acceptance.c" \
    -L"$target/install/lib" -Wl,-rpath,"$target/install/lib" -lucp -lucs \
    -o "$target/install/bin/rm_ctp_acceptance" > "$target/acceptance_build.log" 2>&1
sha256sum "$target/install/lib/ucx/libuct_ub.so.0.0.0" \
    "$target/install/lib/libucp.so.0.0.0" "$target/install/bin/rm_ctp_acceptance" > "$target/install_hashes.txt"
printf 'BUILD_INSTALL_OK=%s\n' "$target"
cat "$target/install_hashes.txt"
