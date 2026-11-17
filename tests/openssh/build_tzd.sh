#!/bin/bash
# OpenSSH 10.3p1 built with TzD (task #59): configure runs with the project
# GCC, then every .c is compiled by cc1tzd.  It uses the patched TzD OpenSSL
# headers (the OSSL_DISPATCH extractor goes through __tzd_fncast under #61)
# and force-includes tests/openssl/tzd-openssl.h (#pragma tzd atomics /
# returns_twice).  Triage pass: `make -k` collects every compile error.
#   tests/openssh/build_tzd.sh   -> work/openssh/tzd/
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
G=$R/work/build-c/gcc; SRC=$R/work/openssh/openssh-${SSHVER:-10.2p1}; B=$R/work/openssh/tzd
SSL=$R/work/openssl/inst-gcc
[ -f "$SSL/lib64/libcrypto.a" ] || bash "$R/tests/openssl/build_gcc.sh" >/dev/null
cat >"$R/work/openssh/cc-gcc.sh" <<CC
#!/bin/sh
exec $G/xgcc -B$G/ "\$@"
CC
cat >"$R/work/openssh/cc-tzd.sh" <<CC
#!/bin/sh
exec $R/bin/tzd --host -w -fcommon -include $R/tests/openssl/tzd-openssl.h -I $R/work/openssl/tzd/include "\$@" -U_FORTIFY_SOURCE
CC
chmod +x "$R/work/openssh/cc-gcc.sh" "$R/work/openssh/cc-tzd.sh"
rm -rf "$B"; cp -a "$SRC" "$B"; cd "$B"; make distclean >/dev/null 2>&1 || true
CC="$R/work/openssh/cc-gcc.sh" ./configure --prefix="$B/inst" \
  --with-ssl-dir="$SSL" --with-zlib --without-pam --disable-strip \
  --with-privsep-path=/var/empty --with-privsep-user=sshd \
  --without-sandbox \
  >cfg-tzd.log 2>&1 || { tail -25 cfg-tzd.log; exit 1; }
# setproctitle(SPT_REUSEARGV) rewrites the whole contiguous argv+environ
# block through argv[0]; TzD bounds argv[0] to its own length and traps on
# that (correctly -- it is a memory-unsafe libc trick).  Disable the
# cosmetic ps-title rewrite (SPT_NONE) rather than widen argv[0]'s bounds.
sed -i 's/#define SPT_TYPE SPT_REUSEARGV/#define SPT_TYPE SPT_NONE/' config.h
# Compile with cc1tzd AND link the essential binaries with cc-tzd.sh (adds
# libc2d) against the TzD libcrypto (no-asm minimal, tests/openssl).
LC=$R/work/openssl/tzd/libcrypto.a
[ -f "$LC" ] || { echo "no TzD libcrypto -- run tests/openssl/run_hmac.sh first"; exit 1; }
make -k -j"${TZD_JOBS:-2}" CC="$R/work/openssh/cc-tzd.sh" \
  LD="$R/work/openssh/cc-tzd.sh" LDFLAGS="-L. -Lopenbsd-compat/" \
  CHANNELLIBS="$LC -lz" \
  sshd sshd-session sshd-auth ssh ssh-keygen ssh-keyscan sftp-server scp sftp >make-tzd.log 2>&1 || true
# Static copies for the guest image (tests/userland/build_image.sh, task
# #59): the guest has no dynamic loader.  On the host keep the dynamic ones:
# static glibc's NSS dlopens the host's libnss_systemd for names missing
# from /etc/passwd (the privsep user) and crashes.
BINS="sshd sshd-session sshd-auth ssh ssh-keygen sftp-server scp sftp"
mkdir -p static dyn; mv $BINS dyn/ 2>/dev/null || true
make -j"${TZD_JOBS:-2}" CC="$R/work/openssh/cc-tzd.sh" \
  LD="$R/work/openssh/cc-tzd.sh" LDFLAGS="-static -L. -Lopenbsd-compat/" \
  CHANNELLIBS="$LC -lz" $BINS >link-static.log 2>&1 || true
mv $BINS static/ 2>/dev/null || true; mv dyn/* . ; rmdir dyn
echo "=== compile errors (cc1tzd), by kind ==="
grep -E "error:|internal compiler error|sorry" make-tzd.log | \
  sed -E 's/^[^ ]+:[0-9]+:[0-9]+: //' | sort | uniq -c | sort -rn | head -20
echo "=== objects: $(find . -name '*.o' | wc -l); binaries linked: ==="
for b in $BINS; do [ -x "$b" ] && echo "  $b" ; done
echo "undefined refs: $(grep -c 'undefined reference' make-tzd.log 2>/dev/null)"
