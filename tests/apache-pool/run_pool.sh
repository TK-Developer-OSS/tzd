#!/bin/bash
# Task #13 demo: overflow between two objects of one Apache pool.
#   base : plain GCC, work/apache objects (alloc.o, ap_snprintf.o)
#   tzd  : Managed C, work/apache-tzd objects (pool-narrow.patch applied)
# For each: 16 bytes (fits) and 32 bytes (runs into the next object).
# Expected: base 16 -> b=neighbor, base 32 -> b=AAAAAAAA (silently
# corrupted); tzd 16 -> b=neighbor, tzd 32 -> trap (exit 134).
# Needs work/apache built and tests/run_apache.sh run once.
set -u
T=$(cd "$(dirname "$0")/.." && pwd); R=$T/..
G=$R/work/build-c/gcc
B=$R/work/apache/src/apache_1.3.42/src
Z=$R/work/apache-tzd
O=$R/work/apache-pool
mkdir -p "$O"
INC="-I$Z/src/os/unix -I$Z/src/include -DLINUX=22 -DUSE_HSREGEX -DNO_DL_NEEDED"
"$R/work/apache/cc14.sh" $INC -c "$T/apache-pool/pool_overflow.c" -o "$O/base.o" || exit 1
"$G/xgcc" -B"$G/" -o "$O/base" "$O/base.o" "$B/main/alloc.o" "$B/ap/ap_snprintf.o" -lm || exit 1
"$R/bin/tzd" --host -w -fgnu89-inline -include "$Z/shim-tzd.h" $INC -c "$T/apache-pool/pool_overflow.c" -o "$O/tzd.o" || exit 1
"$G/xgcc" -B"$G/" -o "$O/tzd" "$O/tzd.o" "$Z/obj/main_alloc.o" "$Z/obj/ap_ap_snprintf.o" "$R/libc2d/build/libc2d.a" -lm || exit 1
fail=0
check() {   # label binary n want-exit want-text
  local out rc
  out=$("$2" "$3" 2>&1); rc=$?
  if [ "$rc" = "$4" ] && echo "$out" | grep -q -- "$5"; then
    echo "ok   $1: exit=$rc $(echo "$out" | head -1)"
  else
    echo "FAIL $1: exit=$rc (want $4, '$5') $out"; fail=1
  fi
}
check "base 16" "$O/base" 16 0 "b=neighbor"
check "base 32" "$O/base" 32 1 "b=AAAAAAAA"
check "tzd 16"  "$O/tzd"  16 0 "b=neighbor"
check "tzd 32"  "$O/tzd"  32 134 "out-of-bounds"
[ $fail = 0 ] && echo "pool: all ok"
exit $fail
