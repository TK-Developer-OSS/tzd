#!/bin/bash
# Managed C tests: compile each tests/managed/*.c with cc1tzd (own parser,
# managed pointers) and check the exit status.  ok_* must exit 0, trap_*
# must stop in tzd_trap (exit 134), err_* must be rejected by the compiler.
# Both .c (Managed C) and .d (TzD) sources.  Run with --nolibc to link
# libc2d only.  A source line `tzd-flags: ...` adds compiler flags for
# that file (e.g. -Werror=return-type).
cd "$(dirname "$0")/managed" || exit 1
TZD=../../bin/tzd
OUT=${TMPDIR:-/tmp}/tzd-managed.$$
mkdir -p "$OUT"
pass=0 fail=0
base_args=("$@")
for src in *.c *.d; do
  name=${src%.*}
  case $name in ok_*) want=0 ;; trap_*) want=134 ;; err_*) want=err ;; *) continue ;; esac
  # shellcheck disable=SC2046
  set -- "${base_args[@]}" $(sed -n 's/.*tzd-flags: *\([^*]*\).*/\1/p' "$src" | head -1)
  if [ $want = err ]; then
    if $TZD "$@" "$src" -o "$OUT/$name" 2>"$OUT/$name.err"; then
      echo "FAIL $name (compiled, want a compile error)"; fail=$((fail+1))
    else
      echo "ok   $name (compile error) $(grep -m1 error: "$OUT/$name.err" | cut -c1-100)"; pass=$((pass+1))
    fi
    continue
  fi
  if ! $TZD "$@" "$src" -o "$OUT/$name" 2>"$OUT/$name.err"; then
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
