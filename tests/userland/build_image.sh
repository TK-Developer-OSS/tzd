#!/bin/bash
# The guest image of the TzD minimal Linux (task #52): an initramfs where
# every program is compiled by TzD.
#   init, sh, oob (tests/userland/*.c); busybox 1.36.1 (build_busybox.sh:
#   ip, ps, wget, telnetd, login, ftpd, rx, ...).
# TZD_STACK selects what else goes in (default "modern"):
#   modern  the base system only (init runs network + telnetd + ftpd);
#           the modern CVE testbed re-start (TK 2026-11-15, task #59),
#           OpenSSL/OpenSSH added later.
#   retro   plus Apache 1.3 + mod_php3 + the MySQL client (static) and the
#           command-line PHP 3 as /usr/bin/php and phpMyAdmin (task #54);
#           init also runs httpd.  Moves to the Linux 2.4 PoC (task #58).
# init runs /etc/rc.  Logins: root with TZD_ROOT_PW (default "tzd"; the
# guest's ports are forwarded on 127.0.0.1 only).
#   tests/userland/build_image.sh      -> work/linux/poc/initramfs.cpio.gz
set -e
STACK=${TZD_STACK:-modern}
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
W=$R/work/linux; O=$W/poc; G=$R/work/build-c/gcc
mkdir -p "$O"
for f in init sh oob; do
  "$R/bin/tzd" --host -O2 -static -o "$O/$f" "$T/$f.c" 2>"$O/$f.log" ||
    { cat "$O/$f.log"; exit 1; }
done
BB=$R/work/busybox/build/busybox
[ -x "$BB" ] || bash "$T/build_busybox.sh" >/dev/null || { echo "busybox build failed"; exit 1; }

if [ "$STACK" = retro ]; then
  # Apache + mod_php3: the TzD objects of tests/php3/build_tzd.sh (which
  # builds tests/run_apache.sh's Apache objects too), linked statically.
  A=$R/work/apache-tzd; P=$R/work/php3
  MC=$R/work/mysql/client-tzd/libmysql/.libs/libmysqlclient.a
  [ -f "$P/tzd/libmodphp3.a" ] && ls "$P"/obj-tzd/*.o >/dev/null 2>&1 ||
    bash "$R/tests/php3/build_tzd.sh" >/dev/null || { echo "php3 build failed"; exit 1; }
  "$G/xgcc" -B"$G/" -static -o "$O/httpd" $(ls "$A"/obj/*.o | grep -v '/modules\.o$') \
    "$P"/obj-tzd/*.o "$A/siglist.o" "$P/tzd/libmodphp3.a" "$MC" \
    "$R/libc2d/build/libc2d.a" -lz -lm -lcrypt -lresolv 2>"$O/httpd.log" ||
    { cat "$O/httpd.log"; exit 1; }
  PC=$P/php-cli-tzd
  [ -x "$PC" ] || bash "$R/tests/php3/build_cli.sh" >/dev/null || { echo "php cli build failed"; exit 1; }
fi

I=$O/initramfs; rm -rf "$I"
mkdir -p "$I"/{bin,usr/bin,dev,proc,sys,tmp,etc}
cp "$O/init" "$I/init"; cp "$O/sh" "$I/bin/sh"; cp "$O/oob" "$I/bin/oob"
cp "$BB" "$I/bin/busybox"
for a in $("$BB" --list); do
  [ "$a" = sh ] || [ -e "$I/bin/$a" ] || ln -s busybox "$I/bin/$a"
done
# GNU coreutils 9.11 compiled by TzD (tests/coreutils/build_tzd.sh, task
# #71) in /usr/bin, stripped; /usr/bin comes before /bin in the login PATH,
# so they are found before busybox's.  ginstall is `install`.
CU=$R/work/coreutils/tzd/src
if [ -x "$CU/ls" ]; then
  for f in "$CU"/*; do
    [ -f "$f" ] && [ -x "$f" ] || continue
    file -b "$f" | grep -q 'ELF 64-bit LSB executable' || continue
    n=$(basename "$f"); [ "$n" = ginstall ] && n=install
    cp "$f" "$I/usr/bin/$n"; strip "$I/usr/bin/$n"
  done
fi
RSH=/bin/sh
BASH_TZD=${BASH_TZD:-$R/work/bash/tzd/bash}
if [ -x "$BASH_TZD" ]; then
  # GNU bash 5.3 compiled by TzD (tests/bash/build_tzd.sh, task #70) is
  # root's login shell; /bin/sh stays the small TzD shell used by init.
  # bash's bundled termcap reads /etc/termcap: entries for the terminals an
  # ssh client is likely to announce, made from the host's terminfo.
  cp "$BASH_TZD" "$I/bin/bash"; RSH=/bin/bash
  for t in xterm-256color xterm screen-256color screen tmux-256color linux vt100 vt220 dumb; do
    infocmp -C "$t" 2>/dev/null | grep -v '^#'
  done >"$I/etc/termcap"
  printf '/bin/sh\n/bin/bash\n' >"$I/etc/shells"
  printf 'PS1='"'"'\\u@tzd:\\w\\$ '"'"'\nexport PATH=/usr/bin:/bin:/usr/sbin:/sbin\n' >"$I/etc/profile"
fi
# The -ftzd-mpx test program (tests/mpx/run_mpx.sh), when it was built.
[ -x "$R/work/mpx/mpx_test" ] && cp "$R/work/mpx/mpx_test" "$I/bin/mpx_test"
[ -x "$R/work/mpx/mpx_probe" ] && cp "$R/work/mpx/mpx_probe" "$I/bin/mpx_probe"
echo "root:x:0:0:root:/:$RSH" >"$I/etc/passwd"
echo 'nobody:x:65534:65534:nobody:/:/bin/false' >>"$I/etc/passwd"
printf 'root:x:0:\ntty:x:5:\nnogroup:x:65534:\n' >"$I/etc/group"
# Shadow passwords (busybox's own parser and SHA-512 crypt, TzD code).
h=$(openssl passwd -6 "${TZD_ROOT_PW:-tzd}")
printf 'root:%s:20000:0:99999:7:::\nnobody:!:20000:0:99999:7:::\n' "$h" >"$I/etc/shadow"
chmod 600 "$I/etc/shadow"
printf '\nTzD Linux: every program here is compiled by TzD.\n\n' >"$I/etc/motd"
chmod 1777 "$I/tmp"

RC_WEB=""
RC_SSH=""
SB=$R/work/openssh/tzd/static
if [ "$STACK" = modern ] && [ -x "$SB/sshd" ]; then
  # OpenSSH 10.2p1 compiled by TzD (tests/openssh/build_tzd.sh, task #59),
  # static.  The host key and a login key live in work/linux/poc/ssh (made
  # once, kept, so the fingerprint stays): log in as root with
  #   ssh -i work/linux/poc/ssh/guest_login -p 18022 root@127.0.0.1
  # KEX is curve25519 only (ML-KEM / libcrux does not run under TzD yet,
  # task #63); no seccomp sandbox (task #62).
  K=$O/ssh; mkdir -p "$K"
  [ -f "$K/ssh_host_ed25519_key" ] ||
    ssh-keygen -q -t ed25519 -N '' -C tzd-guest -f "$K/ssh_host_ed25519_key"
  [ -f "$K/guest_login" ] ||
    ssh-keygen -q -t ed25519 -N '' -C tzd-guest-login -f "$K/guest_login"
  mkdir -p "$I"/usr/sbin "$I"/usr/libexec "$I"/etc/ssh/authorized_keys \
    "$I"/var/empty "$I"/var/run "$I"/var/log
  # sshd (and StrictModes) refuse group/world-writable dirs; the umask
  # here may leave 775.
  chmod 755 "$I" "$I"/etc "$I"/etc/ssh "$I"/etc/ssh/authorized_keys "$I"/var \
    "$I"/var/empty "$I"/usr "$I"/usr/sbin "$I"/usr/libexec
  cp "$SB/sshd" "$I/usr/sbin/sshd"
  cp "$SB/sshd-session" "$SB/sshd-auth" "$SB/sftp-server" "$I/usr/libexec/"
  # scp: the guest end of "scp -O" (the old protocol runs scp -t/-f
  # there); plain scp and sftp use the sftp-server subsystem.
  cp "$SB/ssh" "$SB/ssh-keygen" "$SB/scp" "$SB/sftp" "$I/usr/bin/"
  install -m 600 "$K/ssh_host_ed25519_key" "$I/etc/ssh/"
  install -m 644 "$K/ssh_host_ed25519_key.pub" "$I/etc/ssh/"
  install -m 644 "$K/guest_login.pub" "$I/etc/ssh/authorized_keys/root"
  echo 'sshd:x:74:74:privsep:/var/empty:/bin/false' >>"$I/etc/passwd"
  echo 'sshd:x:74:' >>"$I/etc/group"
  echo 'sshd:!:20000:0:99999:7:::' >>"$I/etc/shadow"
  printf 'passwd: files\ngroup: files\nshadow: files\n' >"$I/etc/nsswitch.conf"
  cat >"$I/etc/ssh/sshd_config" <<'CONF'
Port 22
HostKey /etc/ssh/ssh_host_ed25519_key
PidFile /var/run/sshd.pid
PermitRootLogin prohibit-password
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
AuthorizedKeysFile /etc/ssh/authorized_keys/%u
KexAlgorithms curve25519-sha256,curve25519-sha256@libssh.org
SshdSessionPath /usr/libexec/sshd-session
SshdAuthPath /usr/libexec/sshd-auth
Subsystem sftp /usr/libexec/sftp-server
LogLevel VERBOSE
CONF
  RC_SSH=$'echo TZD-RC: sshd\n/usr/sbin/sshd -f /etc/ssh/sshd_config -E /var/log/sshd.log'
fi
if [ "$STACK" = retro ]; then
  cp "$O/httpd" "$I/bin/httpd"; cp "$PC" "$I/usr/bin/php"
  mkdir -p "$I"/{www/tmp,var/httpd/logs}
  echo '<html><body>Hello from TzD Linux</body></html>' >"$I/www/index.html"
  # PHP pages (tests/php3/pages); MySQL is the host's.
  for f in "$R"/tests/php3/pages/*.php3; do
    sed 's/127\.0\.0\.1:13306/10.0.2.2:13306/' "$f" >"$I/www/$(basename "$f")"
  done
  chmod 1777 "$I/www/tmp"
  # phpMyAdmin 2.2.6 as /www/pma (benchmark page; task #54): the guest reaches
  # the host MySQL at 10.0.2.2:13306, the browser at 127.0.0.1:18082.
  bash "$R/tests/pma/install.sh" "$I/www" 10.0.2.2:13306 http://127.0.0.1:18082/pma/ >/dev/null
  cp "$R/work/apache/src/apache_1.3.42/conf/mime.types" "$I/etc/mime.types" 2>/dev/null ||
    : >"$I/etc/mime.types"
  cat >"$I/etc/httpd.conf" <<'CONF'
ServerType standalone
ServerRoot /var/httpd
PidFile /var/httpd/logs/httpd.pid
ScoreBoardFile /var/httpd/logs/httpd.scoreboard
LockFile /var/httpd/logs/accept.lock
ErrorLog /var/httpd/logs/error_log
LogLevel info
Port 80
ServerName tzd-guest
User "#65534"
Group "#65534"
DocumentRoot /www
TypesConfig /etc/mime.types
DirectoryIndex index.html index.php3
ResourceConfig /dev/null
AccessConfig /dev/null
AddType application/x-httpd-php3 .php3
CONF
  RC_WEB=$'echo TZD-RC: httpd\nhttpd -f /etc/httpd.conf'
fi

{
cat <<'RC'
echo TZD-RC: network
ip link set lo up
ip link set eth0 up
ip addr add 10.0.2.15/24 dev eth0
ip route add default via 10.0.2.2
ip addr show eth0
RC
[ -n "$RC_WEB" ] && printf '%s\n' "$RC_WEB"
[ -n "$RC_SSH" ] && printf '%s\n' "$RC_SSH"
cat <<'RC'
echo TZD-RC: telnetd ftpd
telnetd
tcpsvd -E 0.0.0.0 21 ftpd -w / &
echo TZD-NET-READY
RC
} >"$I/etc/rc"

(cd "$I" && find . | cpio -o -H newc -R 0:0 2>/dev/null | gzip -9) >"$O/initramfs.cpio.gz"
echo "image ($STACK): $O/initramfs.cpio.gz"
