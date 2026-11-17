#!/bin/bash
# OpenSSL 3.5.6 built with the project GCC 14.4 (baseline before TzD).
# Static (no-shared) to match the guest's static linking; installed into
# work/openssl/inst-gcc.  OpenSSH (tests/openssh) links against this.
#   tests/openssl/build_gcc.sh   -> work/openssl/inst-gcc/{lib64,include,bin}
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
G=$R/work/build-c/gcc; S=$R/work/openssl/openssl-3.5.6; I=$R/work/openssl/inst-gcc
[ -d "$S" ] || { echo "no openssl source in $S"; exit 1; }
rm -rf "$I"; cd "$S"
make distclean >/dev/null 2>&1 || true
CC="$G/xgcc" ./Configure linux-x86_64 no-shared no-docs no-tests \
  -B"$G/" --prefix="$I" --openssldir="$I/ssl" >cfg-gcc.log 2>&1 || { tail -20 cfg-gcc.log; exit 1; }
make -j"${TZD_JOBS:-2}" >make-gcc.log 2>&1 || {
  grep -E "Error|error:" make-gcc.log | head -20; echo "(log: $S/make-gcc.log)"; exit 1; }
make install_sw install_ssldirs >inst-gcc.log 2>&1 || { tail -20 inst-gcc.log; exit 1; }
"$I/bin/openssl" version
echo "openssl (GCC): $I"
