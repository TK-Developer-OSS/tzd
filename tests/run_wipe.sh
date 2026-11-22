#!/bin/bash
# Scope exit destroys every automatic local and parameter (TK 2026-11-07),
# also a for statement's own variables.  At -O0 every local is on the
# stack and the parser wipes it at the scope's exit: check the
# tzd_sh_wipe calls in the GIMPLE dump.  When optimizing, the late pass
# (gcc-tzd/tzd-pass.cc, TK 2026-11-17, task #69) wipes, at the end of its
# storage, every variable that the optimizers left in memory, and every
# parameter passed in memory; one kept in registers has nothing to wipe.  The
# registers are cleared at the function's exit: the call-used general
# registers in the epilogue, the vector state by tzd_vregs_clear.  Before a return the
# function's whole frame is overwritten (tzdstack).
T=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/tzd-wipe.$$
mkdir -p "$OUT" && cd "$OUT" || exit 1
cat > wipe.c <<'SRC'
/* Not inlined: addr's address really leaves f.  */
__attribute__ ((noinline)) void g (long *p) { *p += 1; }
int f (int secret)
{
  int local = secret * 2;
  long addr = local;
  g (&addr);
  char buf[16];
  buf[0] = (char) local;
  { long inner = local; buf[1] = (char) inner; }
  for (int idx = 0; idx < 1; idx++) buf[2] = (char) idx;
  return buf[0] + buf[1] + buf[2] + (int) addr;
}
struct big { long a[40]; };
long h (struct big *b);
/* q: a managed pointer parameter, passed in memory; s: never has its
   address taken but is indexed; t: a pointer local that
   stays in registers.  */
__attribute__ ((noinline)) long k (long *q, int n)
{
  struct big s;
  long *t = q + 1;
  for (int i = 0; i < 40; i++) s.a[i] = q[0] + i + *t;
  return s.a[n];
}
int main (void) { long v[2] = { 3, 4 }; return f (3) == 19 && k (v, 2) == 9 ? 0 : 1; }
SRC
fail=0
for o in -O0 -O2; do
  rm -f ./*.gimple
  "$T/../bin/tzd" $o -fdump-tree-gimple wipe.c -o wipe || { echo "FAIL compile $o"; fail=1; continue; }
  ./wipe || { echo "FAIL run $o"; fail=1; }
  "$T/../bin/tzd" $o -S wipe.c -o wipe.s
  # Every return: the vector state by tzd_vregs_clear, the general
  # registers in the epilogue.
  if grep -q "call[[:space:]]*tzd_vregs_clear" wipe.s && grep -q "xorl[[:space:]]*%r11d" wipe.s; then
    echo "ok   $o registers cleared"
  else echo "FAIL $o registers not cleared"; fail=1; fi
  if [ $o = -O0 ]; then
    for v in buf addr; do
      if grep -q "tzd_sh_wipe (&$v," ./*.gimple; then echo "ok   $o $v wiped"
      else echo "FAIL $o $v not wiped"; fail=1; fi
    done
  else
    # When optimizing, the late pass wipes what is in memory by then
    # (counted in the assembly: it runs after the last GIMPLE dump).  In
    # f, addr's address goes to g: one tzd_sh_wipe call.  buf is only
    # indexed and ends up in registers: no wipe.  The parser emits none.
    nf=$(sed -n '/^f:/,/cfi_endproc/p' wipe.s | grep -c "call[[:space:]]*tzd_sh_wipe")
    if [ "$nf" = 1 ] && ! grep -q "tzd_sh_wipe" ./*.gimple; then
      echo "ok   $o f: addr wiped by the late pass, buf in registers"
    else echo "FAIL $o f: $nf wipe calls (expected 1: addr)"; fail=1; fi
  fi
  for v in secret local inner idx; do
    if [ $o = -O0 ]; then
      # -O0 keeps every local on the stack: wiped.
      if grep -q "tzd_sh_wipe (&$v," ./*.gimple; then echo "ok   $o $v wiped (on the stack at -O0)"
      else echo "FAIL $o $v not wiped"; fail=1; fi
    elif grep -q "tzd_sh_wipe (&$v," ./*.gimple; then
      echo "FAIL $o $v given a stack home and wiped (register only)"; fail=1
    else echo "ok   $o $v register only"; fi
  done
  # The late pass, in k: the parameter in memory and the big local are
  # wiped before the return; the pointer local has no stack home.
  # (Counted in the assembly: the pass runs after the last GIMPLE dump.)
  n=$(sed -n '/^k:/,/cfi_endproc/p' wipe.s | grep -c "call[[:space:]]*tzd_sh_wipe")
  if [ $o = -O2 ]; then
    # q (a managed pointer value: no bounds entries, 24 bytes) is wiped by
    # three inline stores of the wipe value; s (its address is still
    # taken by the indexing) by tzd_sh_wipe.
    if [ "$n" = 1 ] \
       && [ "$(sed -n '/^k:/,/cfi_endproc/p' wipe.s | grep -c 'movq[[:space:]]*%r[a-z0-9]*, \(16\|24\|32\)(%rbp)')" = 3 ] \
       && sed -n '/^k:/,/cfi_endproc/p' wipe.s | grep -q 'movabsq[[:space:]]*\$-6510615555426900571'; then
      echo "ok   $o k: q wiped inline, s by tzd_sh_wipe, t register only"
    else echo "FAIL $o k: $n wipe calls (expected 1: s) or q not wiped inline"; fail=1; fi
  elif [ "$n" -ge 2 ]; then echo "ok   $o k: $n wipes"
  else echo "FAIL $o k: $n wipes"; fail=1; fi
  # The stack left behind (tzdstack): before k's return its frame is
  # overwritten, and after each call the callee's return address is zeroed.
  if sed -n '/^k:/,/cfi_endproc/p' wipe.s | grep -q 'movabsq \$0xa5a5a5a5a5a5a5a5, %rsi' \
     && grep -q 'movq \$0, -8(%rsp)' wipe.s; then
    echo "ok   $o frame destroyed before the return, return address zeroed after a call"
  else echo "FAIL $o no frame wipe or no return-address wipe"; fail=1; fi
done
cd / && rm -rf "$OUT"
[ $fail = 0 ] && echo "wipe: all ok"
