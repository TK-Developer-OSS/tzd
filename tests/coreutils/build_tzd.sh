#!/bin/bash
# GNU coreutils 9.11 (the last release before the 2026-04-27 freeze of task
# #59) built with TzD for the guest (task #71): configure runs with the
# project GCC, then every .c (the programs and gnulib) is compiled by
# cc1tzd and linked statically with libc2d.  Triage pass: `make -k`
# collects every compile error.
#   tests/coreutils/build_tzd.sh   -> work/coreutils/tzd/src/<programs>
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
G=$R/work/build-c/gcc; W=$R/work/coreutils; SRC=$W/coreutils-9.11; B=$W/tzd
if [ ! -d "$SRC" ]; then
  # The tarball's signature was checked when it was fetched (work/gnu-keyring.gpg).
  [ -f "$W/dl/coreutils-9.11.tar.xz" ] || { echo "no coreutils tarball (work/coreutils/dl)"; exit 1; }
  tar -C "$W" -xf "$W/dl/coreutils-9.11.tar.xz"
fi
cat >"$W/cc-gcc.sh" <<CC
#!/bin/sh
exec $G/xgcc -B$G/ "\$@"
CC
cat >"$W/cc-tzd.sh" <<CC
#!/bin/sh
exec $R/bin/tzd --host -w -std=gnu23 -D__NO_INLINE__ -D_FORTIFY_SOURCE=0 -include $T/tzd-coreutils.h "\$@"
CC
chmod +x "$W/cc-gcc.sh" "$W/cc-tzd.sh"
rm -rf "$B"; mkdir -p "$B"; cd "$B"
# gl_cv_func_fflush_stdin=yes: glibc's fflush, fseeko and fclose are used as
# they are.  gnulib's replacements read the fields of the host's FILE
# (freadahead, fflush.c: fp->_IO_read_ptr ...), and a FILE * is an opaque
# handle under TzD, no bytes reachable (the rule for fopen & co.): it is
# not made readable for them.
# -std=gnu23: coreutils 9.11 is written for C23 (configure picks it; make's
# CC= override drops it).  -D__NO_INLINE__: glibc's extern inline wrappers
# (bits/error.h: error () through __va_arg_pack) are host code in system
# headers, where no host pointer is accepted; the out-of-line functions are
# used.  -D_FORTIFY_SOURCE=0: gnulib's config.h turns the fortified libc wrappers
# on by itself (__wmemcpy_chk_warn, ...); TzD does those checks.
# No NLS, no libraries beyond libc (acl, attr, capabilities, SELinux, gmp,
# OpenSSL: the guest has none), static.  FORCE_UNSAFE_CONFIGURE: configure
# refuses to run as root otherwise (harmless here).
# The SIMD versions (wc, cksum: AVX2, AVX-512, PCLMUL) use <immintrin.h>
# vector types, which cc1tzd does not take (task #26); the plain versions
# are used.
SIMD="utils_cv_avx2_intrinsic_exists=no utils_cv_avx512_intrinsic_exists=no
  utils_cv_pclmul_intrinsic_exists=no utils_cv_avx2_pclmul_intrinsic_exists=no
  utils_cv_avx512_pclmul_intrinsic_exists=no utils_cv_vmull_intrinsic_exists=no
  utils_cv_neon_intrinsic_exists=no gl_cv_crc_pclmul=no
  gl_cv_func_fflush_stdin=yes"
env $SIMD CC="$W/cc-gcc.sh" LDFLAGS=-static "$SRC/configure" --prefix=/usr \
  --disable-nls --disable-acl --disable-xattr --disable-libcap \
  --without-selinux --without-libgmp --without-openssl --disable-libsmack \
  --enable-no-install-program=stdbuf \
  >cfg-tzd.log 2>&1 || { tail -25 cfg-tzd.log; exit 1; }
# freadahead without the host's FILE fields (see freadahead-tzd.c): its
# object goes first on every link line, through LDFLAGS.
"$W/cc-tzd.sh" -c -O2 -o "$B/freadahead-tzd.o" "$T/freadahead-tzd.c" ||
  { echo "freadahead-tzd.c does not compile"; exit 1; }
make -k -j"${TZD_JOBS:-2}" CC="$W/cc-tzd.sh" LDFLAGS="-static $B/freadahead-tzd.o" \
  >make-tzd.log 2>&1 || true
echo "=== compile errors (cc1tzd), by kind ==="
grep -E "error:|internal compiler error|sorry" make-tzd.log | \
  sed -E 's/^[^ ]+:[0-9]+:[0-9]+: //' | sort | uniq -c | sort -rn | head -40
echo "=== objects: $(find . -name '*.o' | wc -l); programs: $(find src -maxdepth 1 -type f -perm -100 | wc -l)"
echo "undefined refs: $(grep -c 'undefined reference' make-tzd.log 2>/dev/null)"
