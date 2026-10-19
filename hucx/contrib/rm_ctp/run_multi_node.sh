#!/usr/bin/env bash
# Start the multi-peer demo with this experiment's RM transport only.
set -euo pipefail

if [[ "$#" -lt 3 ]]; then
    printf 'usage: %s ISOLATED_INSTALL --server PORT --peers N | --client HOST PORT --id N\n' "$0" >&2
    exit 2
fi
prefix=$(realpath -e "$1")
shift
case "$prefix" in
    /home/y00993710/urma_rm_ctp_lab/experiments/*/install) ;;
    *) printf 'STOP: isolated experiment installation is required.\n' >&2; exit 2 ;;
esac
test -x "$prefix/bin/rm_ctp_multi_acceptance"

exec timeout 310s env -u LD_PRELOAD -u LD_DEBUG \
    LD_LIBRARY_PATH="$prefix/lib:${LD_LIBRARY_PATH-}" \
    UCX_MODULE_DIR="$prefix/lib/ucx" \
    UCX_TLS=rm_urma UCX_NET_DEVICES="${HUCX_DEVICE:-udmac0d1e2:1}" \
    UCX_RM_URMA_PRIORITY="${HUCX_CTP_PRIORITY:-1}" \
    UCX_PROTO_ENABLE=n UCX_LOG_LEVEL="${UCX_LOG_LEVEL:-info}" \
    UCX_RM_URMA_NATIVE_RMA=n UCX_RNDV_THRESH=inf UCX_RM_URMA_TRACE=y \
    UCX_RM_URMA_TEST_DROP_DATA=0 UCX_RM_URMA_TEST_DROP_ACK=0 \
    "$prefix/bin/rm_ctp_multi_acceptance" "$@"
