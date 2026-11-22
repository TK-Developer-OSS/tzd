#!/bin/bash
# Raw-C boundary tests: tests/host/*.c compiled with `tzd --host` (host
# system headers and libc).  ok_* must exit 0, trap_* must exit 134,
# err_* must be rejected by the compiler.  A line `tzd-flags: ...` in a
# source adds compiler flags.
cd "$(dirname "$0")/host" || exit 1
TZD=../../bin/tzd
OUT=${TMPDIR:-/tmp}/tzd-host.$$
mkdir -p "$OUT"
pass=0 fail=0
base_args=("$@")
for src in *.c; do
  name=${src%.c}
  case $name in ok_*) want=0 ;; trap_*) want=134 ;; err_*) want=err ;; *) continue ;; esac
  # A source line `tzd-flags: ...` adds compiler flags (e.g. -static).
  # shellcheck disable=SC2046
  set -- "${base_args[@]}" $(sed -n 's/.*tzd-flags: *\([^*]*\).*/\1/p' "$src" | head -1)
  if [ $want = err ]; then
    if $TZD --host "$@" "$src" -o "$OUT/$name" 2>"$OUT/$name.err"; then
      echo "FAIL $name (compiled, want a compile error)"; fail=$((fail+1))
    else
      echo "ok   $name (compile error) $(grep -m1 error: "$OUT/$name.err" | cut -c1-100)"; pass=$((pass+1))
    fi
    continue
  fi
  if ! $TZD --host "$@" "$src" -o "$OUT/$name" 2>"$OUT/$name.err"; then
    echo "FAIL $name (compile)"; sed 's/^/    /' "$OUT/$name.err"; fail=$((fail+1)); continue
  fi
  "$OUT/$name" >"$OUT/$name.out" 2>&1; rc=$?
  if [ $rc = $want ]; then
    echo "ok   $name (rc=$rc) $(head -c 120 "$OUT/$name.out" | tr '\n' ' ')"; pass=$((pass+1))
  else
    echo "FAIL $name (rc=$rc, want $want)"; sed 's/^/    /' "$OUT/$name.out"; fail=$((fail+1))
  fi
done
rm -rf "$OUT"
echo "pass=$pass fail=$fail"
[ $fail = 0 ]
