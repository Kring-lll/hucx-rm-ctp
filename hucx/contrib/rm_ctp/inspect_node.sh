#!/usr/bin/env bash
# Read-only, short output. No compiler, install, device reconfiguration or logs written.
set -u
printf 'NODE=%s KERNEL=%s\n' "$(hostname)" "$(uname -r)"
header=/usr/include/ub/umdk/urma/urma_types.h
opcode=/usr/include/ub/umdk/urma/urma_opcode.h
for path in "$header" /usr/lib64/liburma.so.0; do
    if [ -e "$path" ]; then rpm -qf "$(readlink -f "$path")" 2>/dev/null; sha256sum "$path"; else printf 'MISSING=%s\n' "$path"; fi
done
grep -nE 'rm_tp_cap|max_read_size|max_write_size' "$header" 2>/dev/null
grep -nE 'URMA_CR_WR_SUSPEND_DONE|URMA_CR_WR_FLUSH_ERR_DONE' "$opcode" 2>/dev/null
urma_admin show --all --whole --dev udmac0d1e2 2>&1 | grep -B3 -A1 -E 'CTP|UTP' | head -n 8
