#!/bin/bash
# OpenSSH 10.3p1 built with the project GCC 14.4 (baseline before TzD),
# against the static OpenSSL 3.5.6 (tests/openssl/build_gcc.sh) and the host
# zlib.  No PAM (the guest uses /etc/passwd + shadow).  Installed into
# work/openssh/inst-gcc.
#   tests/openssh/build_gcc.sh   -> work/openssh/inst-gcc/{sbin/sshd,bin/ssh}
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
G=$R/work/build-c/gcc; S=$R/work/openssh/openssh-${SSHVER:-10.2p1}
SSL=$R/work/openssl/inst-gcc; I=$R/work/openssh/inst-gcc
[ -d "$S" ] || { echo "no openssh source in $S"; exit 1; }
[ -f "$SSL/lib64/libcrypto.a" ] || bash "$R/tests/openssl/build_gcc.sh" >/dev/null
cd "$S"; make distclean >/dev/null 2>&1 || true
CC="$G/xgcc -B$G/" ./configure --prefix="$I" --with-ssl-dir="$SSL" \
  --with-zlib --without-pam --disable-strip \
  --with-privsep-path=/var/empty --with-privsep-user=sshd \
  >cfg-gcc.log 2>&1 || { tail -25 cfg-gcc.log; exit 1; }
make -j"${TZD_JOBS:-2}" >make-gcc.log 2>&1 || {
  grep -E "Error|error:|undefined" make-gcc.log | head -20; echo "(log: $S/make-gcc.log)"; exit 1; }
# Copy the built binaries (make install wants to create /var/empty as root).
mkdir -p "$I/sbin" "$I/bin"
cp sshd "$I/sbin/"; for b in ssh scp sftp ssh-keygen ssh-keyscan ssh-add ssh-agent; do
  [ -f "$b" ] && cp "$b" "$I/bin/"; done
[ -f sftp-server ] && cp sftp-server "$I/sbin/"
"$I/sbin/sshd" -V 2>&1 | head -1 || true
echo "sshd (GCC): $I/sbin/sshd"; ls -la "$I/sbin/sshd" "$I/bin/ssh"
