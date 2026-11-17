#!/bin/bash
# PoC: a minimal Linux where every program is compiled by TzD (task #52).
#   TzD kernel (tests/kernel/build_tzd.sh)
#   init, sh, oob (tests/userland/*.c)
#   busybox 1.36.1 (tests/userland/build_busybox.sh): ip, ls, ps, ...
#   Apache 1.3.42 + mod_php3 (PHP 3.0.18, tests/php3/build_tzd.sh's
#   objects, linked statically here), with the MySQL 3.23 client
# MySQL's server is C++ (g++, not TzD yet, task #55), so it runs on the
# host (127.0.0.1:13306, tests/mysql/build_server.sh); the guest reaches it
# through QEMU's user network at 10.0.2.2:13306.
# init runs /etc/rc (network, httpd) with the TzD sh, then a shell on the
# serial console (no getty).  QEMU forwards 127.0.0.1:PORT to the guest's
# port 80.
#   tests/userland/run_poc.sh [port]       automatic check
#   tests/userland/run_poc.sh -i [port]    interactive (type commands)
# Env: KERNEL=<bzImage>, MEM=<MiB> (1024), KVM=0 (no KVM).
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
W=$R/work/linux; O=$W/poc; G=$R/work/build-c/gcc
INTER=0; [ "${1:-}" = -i ] && { INTER=1; shift; }
PORT=${1:-18082}
K=${KERNEL:-$W/build-tzdk/arch/x86/boot/bzImage}
[ -f "$K" ] || { echo "no kernel: run tests/kernel/build_tzd.sh"; exit 1; }
TZD_STACK=retro bash "$T/build_image.sh" || exit 1
Q=(qemu-system-x86_64 -m "${MEM:-1024}" -nographic -no-reboot -kernel "$K"
   -initrd "$O/initramfs.cpio.gz" -append "console=ttyS0 panic=-1 nokaslr"
   -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:80"
   -device virtio-net-pci,netdev=n0)
# KVM when the host has it (TCG emulation takes minutes to start Apache
# with PHP); KVM=0 forces emulation.
[ "${KVM:-1}" = 1 ] && [ -w /dev/kvm ] && Q+=(-enable-kvm -cpu host)
bash "$R/tests/mysql/server.sh" start >/dev/null || { echo "mysql start failed"; exit 1; }
trap 'bash "$R/tests/mysql/server.sh" stop' EXIT
if [ $INTER = 1 ]; then
  echo "(http://127.0.0.1:$PORT/ is the guest's Apache; quit: exit, or Ctrl-A x)"
  "${Q[@]}"; exit $?
fi

L=$O/poc.log; F=$O/in.fifo
rm -f "$F"; mkfifo "$F"
timeout 300 "${Q[@]}" <"$F" >"$L" 2>&1 &
q=$!
exec 3>"$F"
for i in $(seq 1 240); do grep -q TZD-NET-READY "$L" && break; sleep 1; done
# Apache takes a while to start its children under QEMU's emulation.
for i in $(seq 1 120); do
  [ "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/" || true)" = 200 ] && break
  sleep 1
done
body=$(curl -s -m 10 "http://127.0.0.1:$PORT/" || true)
codes=""
for i in 1 2 3 4 5; do
  codes="$codes$(curl -s -m 10 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/" || true) "
done
nf=$(curl -s -m 10 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/nope" || true)
php=$(curl -s -m 30 "http://127.0.0.1:$PORT/basic.php3?q=hello&n2=42" || true)
ext=$(curl -s -m 30 "http://127.0.0.1:$PORT/ext.php3" || true)
printf 'uploaded line\n' >"$O/upload.txt"
up=$(curl -s -m 30 -F a=x -F b=y -F "userfile=@$O/upload.txt" "http://127.0.0.1:$PORT/post.php3" || true)
printf '\n' >&3; sleep 2
while IFS= read -r c; do printf '%s\n' "$c" >&3; sleep 3; done <<'EOF'
echo TZD-SH-ECHO hello
ps
wget -q -O - http://127.0.0.1/
wget -q -O - http://127.0.0.1/basic.php3 | head -n 2
cat /var/httpd/logs/error_log
dmesg | tail -n 15
oob
echo TZD-SH-AFTER-OOB
exit
EOF
# The kernel has no ACPI power-off ("System halted instead"): stop QEMU
# once init has said it powers off.
for i in $(seq 1 60); do grep -q TZD-POWEROFF "$L" && break; sleep 1; done
sleep 1; kill $q 2>/dev/null
wait $q || true
exec 3>&-
rm -f "$F"
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2' want '$3'"; fail=1; fi; }
has() { if tr -d '\r' <"$L" | grep -aq -- "$2"; then echo "ok   $1"; else echo "FAIL $1: no '$2'"; fail=1; fi; }
has   "init (TzD)"     "TZD-INIT: init (TzD) started"
has   "eth0 (busybox ip)" "inet 10.0.2.15/24"
has   "rc done"        "TZD-NET-READY"
check "GET / (Apache)" "$body" "<html><body>Hello from TzD Linux</body></html>"
check "5 requests"     "$codes" "200 200 200 200 200 "
check "GET /nope"      "$nf" "404"
phas() { if printf '%s' "$2" | grep -q -- "$3"; then echo "ok   $1"; else echo "FAIL $1: no '$3'"; fail=1; fi; }
phas  "PHP page"       "$php" "q=hello n=42"
phas  "PHP class"      "$php" "^class 2$"
phas  "PHP zlib"       "$ext" "^hello gz$"
phas  "PHP xml"        "$ext" "<A><B></B><C></C></A>"
phas  "PHP mysql (host)" "$ext" "^3=o'k$"
phas  "PHP upload"     "$up" "upload upload.txt 14 uploaded line"
has   "sh"             "^TZD-SH-ECHO hello"
has   "httpd running"  "httpd -f /etc/httpd.conf"
has   "httpd started"  "resuming normal operations"
has   "oob dump"       "oob.c:43: tzd: out-of-bounds pointer access"
has   "after oob"      "\(^\|tzd# \)TZD-SH-AFTER-OOB"
has   "poweroff"       "TZD-POWEROFF"
phas  "PHP in the guest (wget)" "$(tr -d '\r' <"$L")" "^PHP 3.0.18$"
if tr -d '\r' <"$L" | grep -a "tzd:" | grep -avq "oob.c:43: tzd:"; then
  echo "FAIL tzd trap:"; tr -d '\r' <"$L" | grep -a -A3 "tzd:" | grep -av "oob.c" | head -8; fail=1
else echo "ok   no tzd trap (other than oob)"; fi
if grep -aq "\[kc2d\]" "$L"; then echo "FAIL kernel trap:"; grep -a "\[kc2d\]" "$L" | head -5; fail=1; fi
[ $fail = 0 ] && echo "poc: all ok" || { echo "poc: FAILED (log: $L)"; exit 1; }
