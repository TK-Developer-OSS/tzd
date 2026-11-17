#!/bin/bash
# The -ftzd-mpx checks on a machine without MPX: the guest (TzD kernel and
# userland) under QEMU's software emulation of a processor with MPX
# (-cpu max,la57=off, no KVM; without 5-level paging: kc2d's bounds table
# is for 48-bit addresses and stops the kernel otherwise).  tests/mpx/run_mpx.sh must have built
# work/mpx/mpx_test; the image build puts it at /bin/mpx_test.  The guest
# of tests/userland/run_live.sh is stopped and restarted this way (slow:
# the boot takes minutes); start it again normally afterwards.
#   tests/mpx/run_tcg.sh
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..; L=$R/tests/userland/run_live.sh
[ -x "$R/work/mpx/mpx_test" ] || { echo "run tests/mpx/run_mpx.sh first"; exit 1; }
bash "$L" stop >/dev/null 2>&1; sleep 2
KVM=0 CPU=${CPU:-max,la57=off} BOOT_WAIT=${BOOT_WAIT:-2400} bash "$L" start | tail -1 || exit 1
run() { WAIT=${2:-25} bash "$L" run "$1"; }
fail=0
out=$(run 'grep -c mpx /proc/cpuinfo' 15)
echo "$out" | grep -q '^1$' && echo "ok   the guest's processor has MPX" || { echo "FAIL no MPX in the guest: $out"; fail=1; }
# (The serial console's shell is the small TzD sh: no $?, so the messages
# are what is checked.)
out=$(run '/bin/mpx_test ok')
echo "$out" | grep -q 'mpx ok' && echo "ok   in bounds: runs with MPX enabled" || { echo "FAIL in bounds: $out"; fail=1; }
for w in over under heap end; do
  out=$(run "/bin/mpx_test $w")
  if echo "$out" | grep -q 'MPX bound check' && ! echo "$out" | grep -q 'not stopped'; then echo "ok   $w: stopped by #BR"
  else echo "FAIL $w: $out"; fail=1; fi
done
out=$(run '/bin/mpx_test null')
echo "$out" | grep -q 'null pointer access' && echo "ok   null: stopped (software test)" || { echo "FAIL null: $out"; fail=1; }
# The emulation itself: the probe (plain C) shows which forms it handles.
if [ -x "$R/work/mpx/mpx_probe" ]; then
  out=$(run '/bin/mpx_probe trap' 15)
  echo "$out" | grep -q 'NOT TRAPPED' && { echo "FAIL probe: an out-of-bounds bndcu did not trap"; fail=1; } || echo "ok   probe: out-of-bounds bndcu traps"
fi
[ $fail = 0 ] && echo "mpx (emulated processor): all ok"
exit $fail
