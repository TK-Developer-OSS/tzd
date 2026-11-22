#!/bin/bash
# Native calling-convention functions of the kernel (task #42): the global
# symbols defined in, and referenced from, the objects assembled from .S
# files that are linked into vmlinux (not the vDSO, the decompressor or
# real-mode code).  Data symbols in the list do no harm: cc1tzd applies it
# to function declarations only.
#   tests/kernel/gen_native_list.sh <kernel build dir>   -> stdout
set -e
B=$1; [ -d "$B" ] || { echo "usage: $0 <kernel build dir>" >&2; exit 1; }
cd "$B"
find . -name '.*.o.cmd' ! -path './arch/x86/entry/vdso/*' ! -path './arch/x86/boot/*' \
     ! -path './arch/x86/realmode/*' ! -path './scripts/*' ! -path './tools/*' \
  | while read -r c; do
      grep -q '^source_.*\.S$' "$c" || continue
      o=$(dirname "$c")/$(basename "$c" .cmd | sed 's/^\.//')
      [ -f "$o" ] && echo "$o"
    done | sort | xargs -r nm 2>/dev/null \
  | awk '$1 == "U" { print $2; next } $2 ~ /^[TW]$/ { print $3 }' \
  | grep -v -E '^(tzd_|__tzd)' | sort -u
