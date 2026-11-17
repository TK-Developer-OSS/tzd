#!/bin/bash
# HMAC-SHA256 computed by the TzD-compiled OpenSSL libcrypto, checked against
# the reference (GCC) OpenSSL (task #59/#61).  End-to-end proof that TzD's
# OpenSSL crypto is correct: a minimal, pure-C (no-asm) libcrypto, so every
# object is compiled by cc1tzd with bounds checks live and the #61 dispatch
# contract (tzd_fn_check) running.  The minimal config (no-threads/dso/
# engine/sock/asm) sidesteps the C<->asm native boundary and the host
# pthread-callback / TSD gaps, which are separate TzD work.
#   tests/openssl/run_hmac.sh [-f]      -f: force a rebuild of libcrypto
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
L=$R/work/openssl/tzd/libcrypto.a; REF=$R/work/openssl/inst-gcc/bin/openssl
MSG="The quick brown fox jumps over the lazy dog"
[ -x "$REF" ] || bash "$T/build_gcc.sh" >/dev/null
if [ ! -f "$L" ] || [ "$1" = "-f" ]; then
  echo "building minimal no-asm TzD libcrypto (this recompiles all of it)..."
  OSSL_EXTRA="no-threads no-dso no-engine no-sock no-dgram no-ui-console no-http no-asm" \
    bash "$T/build_tzd.sh" >/dev/null || { echo "libcrypto build failed"; exit 1; }
fi
B=$R/work/openssl/hmac; mkdir -p "$B"
# Any truly-undefined __tzd_size (an array defined in assembly, e.g.
# OPENSSL_ia32cap_P -- none with no-asm) gets a stub at its real byte size.
nm "$L" 2>/dev/null | grep -E ' [TtDdRrVvWw] __tzd_size\.' | sed 's/.*__tzd_size\.//' | sort -u >"$B/def"
nm "$L" 2>/dev/null | grep ' U __tzd_size\.' | sed 's/.*__tzd_size\.//' | sort -u >"$B/ref"
comm -23 "$B/ref" "$B/def" >"$B/undef"
SZ=""
if [ -s "$B/undef" ]; then
  { echo ".section .rodata"; echo ".align 8"; } >"$B/sz.s"
  while read -r nmn; do
    hs=$(nm -S --defined-only "$L" 2>/dev/null | awk -v x="$nmn" '$4==x&&$2~/^[0-9a-f]+$/{print $2;exit}')
    [ -z "$hs" ] && d=64 || d=$((16#$hs))
    printf '.globl __tzd_size.%s\n__tzd_size.%s:\n    .quad %d\n' "$nmn" "$nmn" "$d" >>"$B/sz.s"
  done <"$B/undef"
  "$R/work/build-c/gcc/xgcc" -B"$R/work/build-c/gcc/" -c -o "$B/sz.o" "$B/sz.s"
  SZ="$B/sz.o"
fi
"$R/bin/tzd" --host -O2 -include "$T/tzd-openssl.h" -I "$R/work/openssl/tzd/include" \
  -o "$B/hmac" "$T/hmac_test.c" -Wl,--whole-archive "$L" -Wl,--no-whole-archive $SZ 2>/dev/null
out=$("$B/hmac")
ref=$(printf '%s' "$MSG" | "$REF" dgst -sha256 -hmac key 2>/dev/null | sed 's/.*= *//')
echo "TzD libcrypto: $out"
echo "reference    : $ref"
[ -n "$out" ] && [ "$out" = "$ref" ] && { echo "HMAC-SHA256 MATCH"; exit 0; } || { echo "MISMATCH"; exit 1; }
