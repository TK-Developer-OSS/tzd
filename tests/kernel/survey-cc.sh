#!/bin/bash
# Compiler wrapper for the kernel survey (task #42): every kernel .c file
# is first compiled by cc1tzd (bin/tzd --host: kernel headers are plain
# -I headers, so all of them are Managed C).  When cc1tzd rejects a file,
# its errors are recorded in $TZD_SURVEY/fail/ and the file is compiled by
# plain GCC so that one build pass collects every error.  This fallback
# exists only for the survey; the kernel it builds is not the deliverable.
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
G=$R/work/build-c/gcc
src= ; compile=0 ; bits64=1
for a in "$@"; do
  case "$a" in
    -c) compile=1 ;;
    -m16|-m32) bits64=0 ;;
    *.c) src=$a ;;
  esac
done
key=$(printf '%s' "$src" | tr '/' '_')
if [ $compile = 1 ] && [ -n "$src" ] && [ -n "$TZD_SURVEY" ] && [ $bits64 = 0 ]; then
  # 16/32-bit boot and real-mode code: TzD is x86_64 only.
  mkdir -p "$TZD_SURVEY/skip"; : >"$TZD_SURVEY/skip/$key"
elif [ $compile = 1 ] && [ -n "$src" ] && [ -n "$TZD_SURVEY" ]; then
  # The first 20000 bytes of diagnostics are kept, the rest is discarded
  # (not refused: a file with many warnings still compiles), and a compile
  # is stopped after 300 s, so a compiler that loops printing errors cannot
  # fill the disk (2026-11-12: 4 files grew to 2.8-8.1 GB).
  rcf="$TZD_SURVEY/rc.$$"
  # Only errors and their context are kept: warnings filled the cap before
  # the first error in some files (2026-11-13).
  ( timeout 300 "$R/bin/tzd" --host "$@" 2>&1 >/dev/null; echo $? >"$rcf" ) \
    | grep -E -B3 -A6 ': error:|internal compiler error|fatal error' \
    | { head -c 20000 >"$TZD_SURVEY/err.$$"; cat >/dev/null; }
  rc=$(cat "$rcf" 2>/dev/null || echo 1); rm -f "$rcf"
  if [ "$rc" = 0 ]; then
    : >"$TZD_SURVEY/ok/$key"
    rm -f "$TZD_SURVEY/err.$$"
    exit 0
  fi
  { echo "$src"; head -c 20000 "$TZD_SURVEY/err.$$"; } >"$TZD_SURVEY/fail/$key"
  rm -f "$TZD_SURVEY/err.$$"
fi
exec "$G/xgcc" -B"$G/" "$@"
