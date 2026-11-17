#!/bin/bash
# cc1tzd's -ftzd-mpx (experimental, TK 2026-11-17): the bounds checks as
# Intel MPX instructions.  Builds tests/mpx/mpx_test.c both ways and checks
#   - the MPX build contains bndmk/bndcl/bndcu and the .preinit_array
#     entry for tzd_mpx_init, and no bounds trap calls for the checks;
#   - on this machine: with MPX (Skylake..Comet Lake and the OS state
#     enabled) the in-bounds run passes and each out-of-bounds run stops
#     with the MPX message (exit 134); without MPX the program refuses to
#     start (exit 134) instead of running unchecked.
# The checks themselves are exercised on a machine without MPX by
# tests/mpx/run_tcg.sh (QEMU's software emulation of an MPX processor).
#   tests/mpx/run_mpx.sh   -> work/mpx/mpx_test (MPX), work/mpx/mpx_test_sw
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..; W=$R/work/mpx
mkdir -p "$W"; fail=0
"$R/bin/tzd" --host -O2 -static -ftzd-mpx -o "$W/mpx_test" "$T/mpx_test.c" || { echo "FAIL compile (MPX)"; exit 1; }
"$R/bin/tzd" --host -O2 -static -o "$W/mpx_test_sw" "$T/mpx_test.c" || { echo "FAIL compile (software)"; exit 1; }
"$R/bin/tzd" --host -O2 -ftzd-mpx -S -o "$W/mpx_test.s" "$T/mpx_test.c"
for i in bndmk bndcl bndcu; do
  if grep -q "$i" "$W/mpx_test.s"; then echo "ok   $i in the MPX build ($(grep -c "$i" "$W/mpx_test.s"))"
  else echo "FAIL no $i in the MPX build"; fail=1; fi
done
if grep -q "preinit_array" "$W/mpx_test.s" && grep -q "tzd_mpx_init" "$W/mpx_test.s"; then
  echo "ok   .preinit_array entry for tzd_mpx_init"
else echo "FAIL no .preinit_array entry"; fail=1; fi
[ "$("$W/mpx_test_sw" ok)" = "mpx ok" ] && echo "ok   software build runs" || { echo "FAIL software build"; fail=1; }
run() { out=$("$W/mpx_test" "$@" 2>&1); rc=$?; }
if grep -qw mpx /proc/cpuinfo; then
  run ok; [ $rc = 0 ] && [ "$out" = "mpx ok" ] && echo "ok   in bounds: runs" || { echo "FAIL in bounds: rc=$rc $out"; fail=1; }
  for w in over under heap end; do
    run $w
    if [ $rc = 134 ] && echo "$out" | grep -q "MPX bound check"; then echo "ok   $w: stopped by #BR"
    else echo "FAIL $w: rc=$rc $out"; fail=1; fi
  done
  run null; [ $rc = 134 ] && echo "ok   null: stopped" || { echo "FAIL null: rc=$rc"; fail=1; }
else
  run ok
  if [ $rc = 134 ] && echo "$out" | grep -q "no MPX"; then
    echo "ok   no MPX on this processor: the program refuses to start (not unchecked)"
  else echo "FAIL without MPX the program must stop: rc=$rc $out"; fail=1; fi
fi
[ $fail = 0 ] && echo "mpx: all ok"
exit $fail
