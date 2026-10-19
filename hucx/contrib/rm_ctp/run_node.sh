#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -lt 3 ]; then echo "usage: $0 ISOLATED_PREFIX --server PORT | --client HOST PORT [--tag-only] [--iters N]" >&2; exit 2; fi
prefix=$(realpath -e "$1")
shift
case "$prefix" in */experiments/*/install) ;; *) echo 'STOP: an isolated experiments/.../install prefix is required' >&2; exit 2;; esac
test -x "$prefix/bin/rm_ctp_acceptance"
exec timeout 310s env -u LD_PRELOAD -u LD_DEBUG \
    LD_LIBRARY_PATH="$prefix/lib:${LD_LIBRARY_PATH-}" UCX_MODULE_DIR="$prefix/lib/ucx" \
    UCX_TLS=rm_urma UCX_NET_DEVICES="${HUCX_DEVICE:-udmac0d1e2:1}" \
    UCX_RM_URMA_PRIORITY="${HUCX_CTP_PRIORITY:-1}" \
    UCX_PROTO_ENABLE=n UCX_LOG_LEVEL="${UCX_LOG_LEVEL:-info}" \
    "$prefix/bin/rm_ctp_acceptance" "$@"
