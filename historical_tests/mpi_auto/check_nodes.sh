#!/usr/bin/env bash
set -euo pipefail
test "$(hostname -s)" = node89 || { echo 'STOP: run on node89'; exit 1; }
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
(cd "$bundle" && sha256sum -c SHA256SUMS)
command -v python3
for node in local node97; do
    printf '\nPREFLIGHT=%s\n' "$node"
    if [[ "$node" == local ]]; then
        bash "$bundle/check_runtime.sh"
    else
        ssh root@9.88.43.97 'bash -s' < "$bundle/check_runtime.sh"
    fi
done
printf 'AUTO_TEST_PREFLIGHT_OK\n'
