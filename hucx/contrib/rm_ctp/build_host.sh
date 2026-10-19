#!/usr/bin/env bash
# Compile/test only: generates a deliberately nonfunctional mock SDK.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo "usage: $0 UMDK_SOURCE NEW_ABSOLUTE_STAGE" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/../.." && pwd)
umdk=$(realpath -e "$1")
stage=$(realpath -m "$2")
case "$stage" in "$root"|"$root"/*) echo 'STOP: stage must be outside source' >&2; exit 2;; esac
if [ -e "$stage" ] || [ -L "$stage" ]; then echo 'STOP: stage exists' >&2; exit 2; fi
headers="$umdk/src/urma/lib/urma/core/include"
test -f "$headers/urma_api.h"
printf '#include <infiniband/verbs.h>\n' | ${CC:-gcc} -x c -fsyntax-only -
mkdir "$stage"
mkdir -p "$stage/sdk/include" "$stage/sdk/lib" "$stage/source" "$stage/build"
cp "$headers/"*.h "$stage/sdk/include/"
tar -C "$root" --exclude='./.git' --exclude='./autom4te.cache' -cf - . | tar -C "$stage/source" -xf -
python3 - "$stage" <<'PY'
import pathlib,re,sys
stage=pathlib.Path(sys.argv[1]); include=stage/'sdk/include'
api=re.sub(r'/\*.*?\*/','', (include/'urma_api.h').read_text(),flags=re.S)
prototypes={name:(ret,args) for ret,name,args in re.findall(r'^\s*(\w+(?:\s*\*+)?)\s*(urma_\w+)\s*\(([^;{}]*?)\)\s*;',api,flags=re.M)}
needed=set()
for p in (stage/'source/src/uct/urma').rglob('*'):
    if p.suffix in ('.c','.h'):
        needed.update(re.findall(r'\b(urma_\w+)\s*\(',p.read_text()))
missing=needed-prototypes.keys()
if missing: raise SystemExit('Missing API prototypes: '+str(sorted(missing)))
code=['#include <urma_api.h>','#include <stddef.h>']
for name in sorted(needed):
    ret,args=prototypes[name]
    value='return;' if ret=='void' else ('return NULL;' if '*' in ret else 'return ('+ret+')-1;')
    code.append(ret+' '+name+'('+args+') { '+value+' }')
(stage/'sdk/stubs.c').write_text('\n'.join(code)+'\n')
(stage/'sdk/NOT_A_REAL_SDK.txt').write_text('Compilation shim only. Every URMA operation fails. Never deploy these libraries to a node.\n')
print('MOCK_API_SYMBOLS='+str(len(needed)))
PY
${CC:-gcc} -shared -fPIC -Werror -Wno-unused-parameter -I"$stage/sdk/include" \
    "$stage/sdk/stubs.c" -o "$stage/sdk/lib/liburma.so"
printf 'int rm_ctp_mock_common;\n' | ${CC:-gcc} -shared -fPIC -x c - -o "$stage/sdk/lib/liburma_common.so"
(cd "$stage/source" && ./autogen.sh) > "$stage/autogen.log" 2>&1
if [ "${HUCX_SANITIZE:-0}" = 1 ]; then
    # Public UCX wire packing/mpool/list helpers trigger these two checks.
    # The standalone reliable core keeps the full sanitizer set.
    cflags='-O1 -g -fsanitize=address,undefined -fno-sanitize=alignment,pointer-overflow -fno-omit-frame-pointer -Wno-error=format-truncation'
    ldflags='-fsanitize=address,undefined'
else cflags='-O2 -g'; ldflags=''; fi
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
(cd "$stage/build" && CFLAGS="$cflags" CXXFLAGS="$cflags" LDFLAGS="$ldflags" LIBS='-lstdc++ -lrt' \
    ../source/configure --prefix="$stage/host-install" --with-ub="$stage/sdk" \
    --without-java --without-cuda --without-rocm --without-verbs --without-rdmacm) > "$stage/configure.log" 2>&1
make -C "$stage/build" -j"${HUCX_JOBS:-4}" > "$stage/build.log" 2>&1
export LD_LIBRARY_PATH="$stage/sdk/lib:${LD_LIBRARY_PATH-}"
"$stage/source/contrib/rm_ctp/test_software.sh" > "$stage/reliable-test.log" 2>&1
"$stage/source/contrib/rm_ctp/test_transport.sh" "$stage/build" "$stage/sdk/include" > "$stage/transport-test.log" 2>&1
"$stage/source/contrib/rm_ctp/test_ucp.sh" "$stage/build" "$stage/sdk/include" > "$stage/ucp-test.log" 2>&1
printf 'HOST_COMPILE_AND_SOFTWARE_TESTS_OK=%s\n' "$stage"
tail -n 1 "$stage/reliable-test.log"
tail -n 1 "$stage/transport-test.log"
tail -n 1 "$stage/ucp-test.log"
