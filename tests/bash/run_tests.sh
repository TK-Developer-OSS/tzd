#!/bin/bash
# bash's own test suite (tests/run-*) against a built bash, one test at a
# time, in the C locale; prints the tests whose output differs from the
# expected (*.right).  Used to compare the TzD bash (task #70) with the
# same source built by plain GCC.
#   tests/bash/run_tests.sh BUILD-DIR   -> BUILD-DIR/tests-by-name/<test>.out
set -u
B=$(cd "$1" && pwd); O=$B/tests-by-name; rm -rf "$O"; mkdir -p "$O"
cd "$B/tests" || exit 1
# What tests/run-all sets up (BASH_TSTOUT: each test's output file).
export LC_ALL=C LANG=C THIS_SH=$B/bash PATH=.:$B:$PATH BUILD_DIR=$B
export TMPDIR=$(mktemp -d) BASH_TSTOUT
unset BASH_ENV SHELLOPTS BASHOPTS
for t in run-*; do
  [ "$t" = run-all ] || [ "$t" = run-minimal ] && continue
  BASH_TSTOUT=$TMPDIR/bashtst-$t
  timeout 300 "$THIS_SH" "./$t" >"$O/$t.out" 2>&1
  rc=$?
  if [ -s "$O/$t.out" ] || [ $rc != 0 ]; then echo "$t rc=$rc lines=$(wc -l <"$O/$t.out")"; fi
done
rm -rf "$TMPDIR"
