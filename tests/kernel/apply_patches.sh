#!/bin/sh
# Apply the TzD-side kernel changes (tests/kernel/patches/*.patch, task
# #42: source the TzD rules require to be written differently) to the
# kernel tree given as $1.  Each patch is applied once: one that already
# applies in reverse is skipped; one that applies neither way stops.
set -e
T=$(cd "$(dirname "$0")" && pwd); K=$1
for p in "$T"/patches/*.patch; do
  if patch -d "$K" -p1 -R -s -f --dry-run <"$p" >/dev/null 2>&1; then
    continue
  fi
  patch -d "$K" -p1 -s -f <"$p" || { echo "patch failed: $p" >&2; exit 1; }
done
