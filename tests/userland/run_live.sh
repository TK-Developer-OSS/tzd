#!/bin/bash
# The TzD minimal Linux kept running (TK 2026-11-14: improve it live).
#   tests/userland/run_live.sh start     build the image, boot it in the
#                                        background (KVM when available)
#   tests/userland/run_live.sh stop      power it off (QEMU quits)
#   tests/userland/run_live.sh status
#   tests/userland/run_live.sh console   the serial console (socat; Ctrl-C)
#   tests/userland/run_live.sh put FILE [GUEST-PATH]
#                                        send FILE (HTTP from the host,
#                                        the guest's wget; default /tmp)
#   tests/userland/run_live.sh xput FILE [GUEST-PATH]
#                                        the same over the serial line
#                                        with xmodem (guest: busybox rx)
#   tests/userland/run_live.sh run 'CMD' run CMD on the serial console
# Ports on 127.0.0.1: 18082 HTTP (Apache + PHP), 18023 telnet (login root,
# password TZD_ROOT_PW at build time, default "tzd").  MySQL: the host's
# test server (tests/mysql/server.sh), 10.0.2.2:13306 from the guest.
# Env: KVM=0 CPU=<model> BOOT_WAIT=<seconds> (QEMU's software emulation of
# another processor, e.g. CPU=max for MPX: tests/mpx/run_tcg.sh; slow),
# KERNEL, MEM (MiB, 2048: TK 2026-11-16, the 512 MB bounds-table pool
# is in the kernel image and sshd sessions are large under libc2d), KVM=0.
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
W=$R/work/linux; O=$W/poc; V=$O/live
K=${KERNEL:-$W/build-tzdk/arch/x86/boot/bzImage}
SER=$V/serial.sock; MON=$V/monitor.sock; PID=$V/qemu.pid; LOG=$V/console.log
running() { [ -f "$PID" ] && kill -0 "$(cat "$PID")" 2>/dev/null; }
# A command line on the serial console (the root shell init started).
send() { printf '%s\n' "$1" | socat -t 1 - UNIX-CONNECT:"$SER" >/dev/null; }
case ${1:-} in
start)
  running && { echo "already running (pid $(cat "$PID"))"; exit 0; }
  [ -f "$K" ] || { echo "no kernel: run tests/kernel/build_tzd.sh"; exit 1; }
  bash "$T/build_image.sh" || exit 1
  bash "$R/tests/mysql/server.sh" start >/dev/null || { echo "mysql start failed"; exit 1; }
  mkdir -p "$V"; rm -f "$SER" "$MON"
  Q=(qemu-system-x86_64 -m "${MEM:-2048}" -display none -no-reboot
     -kernel "$K" -initrd "$O/initramfs.cpio.gz" -append "console=ttyS0 panic=-1 nokaslr"
     -chardev "socket,id=s0,path=$SER,server=on,wait=off,logfile=$LOG"
     -serial chardev:s0 -monitor "unix:$MON,server,nowait"
     -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:18082-:80,hostfwd=tcp:127.0.0.1:18023-:23,hostfwd=tcp:127.0.0.1:18022-:22"
     -device virtio-net-pci,netdev=n0 -pidfile "$PID" -daemonize)
  if [ "${KVM:-1}" = 1 ] && [ -w /dev/kvm ]; then Q+=(-enable-kvm -cpu host)
  elif [ -n "${CPU:-}" ]; then Q+=(-cpu "$CPU"); fi
  "${Q[@]}" || exit 1
  for i in $(seq 1 "${BOOT_WAIT:-120}"); do grep -aq TZD-NET-READY "$LOG" 2>/dev/null && break; sleep 1; done
  grep -aq TZD-NET-READY "$LOG" || { echo "no TZD-NET-READY (log: $LOG)"; exit 1; }
  echo "running (pid $(cat "$PID")): http://127.0.0.1:18082/  telnet 127.0.0.1 18023  ssh -p 18022 root@127.0.0.1"
  echo "console log: $LOG" ;;
stop)
  running || { echo "not running"; exit 0; }
  send exit; sleep 3
  running && echo quit | socat -t 1 - UNIX-CONNECT:"$MON" >/dev/null
  sleep 1; running && kill "$(cat "$PID")"
  bash "$R/tests/mysql/server.sh" stop
  echo stopped ;;
status)
  if running; then echo "running (pid $(cat "$PID"))"; else echo "not running"; fi ;;
console)
  running || { echo "not running"; exit 1; }
  socat -,raw,echo=0,escape=0x1d UNIX-CONNECT:"$SER" ;;   # quit: Ctrl-]
run)
  running || { echo "not running"; exit 1; }
  n=$(wc -c <"$LOG"); send "$2"; sleep "${WAIT:-2}"; tail -c +"$((n + 1))" "$LOG" | tr -d '\r' ;;
put)
  # Over the network: a one-file HTTP server on the host (127.0.0.1:18080,
  # the guest's 10.0.2.2:18080), fetched with the guest's wget.
  running || { echo "not running"; exit 1; }
  f=$(realpath "$2"); dst=${3:-/tmp/$(basename "$f")}
  [ -f "$f" ] || { echo "no file $f"; exit 1; }
  python3 -I -m http.server 18080 --bind 127.0.0.1 --directory "$(dirname "$f")" \
    >/dev/null 2>&1 & hs=$!
  sleep 1
  n=$(wc -c <"$LOG")
  # The TzD sh has no &&: one command after another, then the size check.
  send "rm -f $dst.t; wget -q -O $dst.t http://10.0.2.2:18080/$(basename "$f"); chmod 755 $dst.t; mv $dst.t $dst; wc -c $dst"
  for i in $(seq 1 120); do tail -c +"$((n + 1))" "$LOG" | grep -aq "^ *[0-9]* $dst" && break; sleep 1; done
  kill $hs 2>/dev/null
  tail -c +"$((n + 1))" "$LOG" | tr -d '\r' | grep -a "$dst" | tail -1 ;;
xput)
  # Over the serial line with xmodem (guest: busybox rx; slow: 128-byte
  # blocks, one acknowledgement each).
  running || { echo "not running"; exit 1; }
  f=$2; dst=${3:-/tmp/$(basename "$f")}
  [ -f "$f" ] || { echo "no file $f"; exit 1; }
  send "rx $dst"; sleep 1
  # xmodem pads the last 128-byte block with ^Z; the guest trims it to
  # the real size afterwards.
  socat EXEC:"sx -q $f",pty,raw,echo=0 UNIX-CONNECT:"$SER" 2>/dev/null
  sleep 1; send "dd if=$dst of=$dst.t bs=1 count=$(wc -c <"$f") 2>/dev/null; mv $dst.t $dst"
  sleep 1; n=$(wc -c <"$LOG"); send "wc -c $dst"; sleep 1
  tail -c +"$((n + 1))" "$LOG" | tr -d '\r' | grep -a "$dst" ;;
*) sed -n 2,16p "$0"; exit 2 ;;
esac
