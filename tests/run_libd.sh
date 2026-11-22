#!/bin/sh
# Build libc2d and run the libc2d tests with no glibc (own _start).
# These tests are written in the hand-lowered form (explicit tzd_ptr and tzd_*
# calls), i.e. what cc1tzd emits, so they are compiled as plain C (cc1),
# like libc2d itself -- not through cc1tzd, which would lower them again.
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/..
O=${TMPDIR:-/tmp}/libd-tests; mkdir -p "$O"
make -C "$R/libc2d" >/dev/null
for t in libd_hello libd_overflow libd_malloc libd_string; do
  G=$R/work/build-c/gcc
  "$G/xgcc" -B"$G/" -nostdinc -I"$R/libc2d/include" -nostdlib -nostartfiles \
    -ffreestanding -fno-stack-protector -O0 -Wall \
    "$R/libc2d/build/crt0.o" -x c "$T/$t.c" -x none "$R/libc2d/build/libc2d.a" -o "$O/$t"
done
echo "--- libd_hello (expect: Hello, World!  rc=0)"
"$O/libd_hello"; echo "rc=$?"
set +e
echo "--- libd_overflow (expect: ok line, trap message, rc=134)"
"$O/libd_overflow"; echo "rc=$?"
echo "--- libd_malloc (expect: abcdefghij, len=10, freed, trap message, rc=134)"
"$O/libd_malloc"; echo "rc=$?"
echo "--- libd_string (expect: libd_string: all ok, then trap message, rc=134)"
"$O/libd_string"; echo "rc=$?"
