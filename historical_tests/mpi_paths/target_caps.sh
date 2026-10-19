#!/usr/bin/env bash
set -euo pipefail
(( $# == 1 )) || { echo 'Usage: bash target_caps.sh EXISTING_UCX_INFO_LOG'; exit 1; }
awk '
/Transport:/ {transport=$0; show=0}
/Device:/ {
    show=(transport ~ /Transport: rm_urma([[:space:]]|$)/ &&
          $0 ~ /Device: udmac0d1e2:1([[:space:]]|$)/)
    if (show) print transport
}
show && /Memory domain:/ {show=0}
show {print; found=1}
END {if (!found) {print "TARGET_CAPS_NOT_FOUND"; exit 1}}
' "$1"
