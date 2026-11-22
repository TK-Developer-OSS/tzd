#!/bin/bash
# Boot the TzD kernel with a TzD init and a TzD sh (task #42, TK
# 2026-11-13: "sh usable, getty may be left out").
#   tests/userland/run_sh.sh                 automatic check
#   tests/userland/run_sh.sh -i              interactive (type commands)
# /init, /bin/sh and /bin/oob are tests/userland/{init,sh,oob}.c built with bin/tzd
# --host -static; the other commands are busybox built with TzD
# (tests/userland/build_busybox.sh): every program in the guest is TzD's.  The automatic check waits for init's banner, types a
# few commands on the serial console and checks their output.
# Env: KERNEL=<bzImage> (default: the TzD kernel), MEM=<MiB> (1024).
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
W=$R/work/linux; O=$W/userland
K=${KERNEL:-$W/build-tzdk/arch/x86/boot/bzImage}
[ -f "$K" ] || { echo "no kernel: run tests/kernel/build_tzd.sh"; exit 1; }
mkdir -p "$O"
for f in init sh oob; do
  "$R/bin/tzd" --host -O2 -static -o "$O/$f" "$T/$f.c" 2>"$O/$f.log" ||
    { cat "$O/$f.log"; exit 1; }
done
I=$O/initramfs; rm -rf "$I"; mkdir -p "$I/bin" "$I/dev" "$I/proc" "$I/sys" "$I/tmp"
cp "$O/init" "$I/init"; cp "$O/sh" "$I/bin/sh"; cp "$O/oob" "$I/bin/oob"
# The other commands: busybox built with TzD (build_busybox.sh).
BB=$R/work/busybox/build/busybox
[ -x "$BB" ] || bash "$T/build_busybox.sh" >/dev/null || { echo "busybox build failed"; exit 1; }
cp "$BB" "$I/bin/busybox"
for a in $("$BB" --list); do
  [ "$a" = sh ] || [ -e "$I/bin/$a" ] || ln -s busybox "$I/bin/$a"
done
(cd "$I" && find . | cpio -o -H newc 2>/dev/null | gzip -9) >"$O/initramfs.cpio.gz"
Q=(qemu-system-x86_64 -m "${MEM:-1024}" -nographic -no-reboot -kernel "$K"
   -initrd "$O/initramfs.cpio.gz" -append "console=ttyS0 panic=-1 nokaslr")
if [ "$1" = -i ]; then
  exec "${Q[@]}"
fi

L=$O/sh.log; F=$O/in.fifo
rm -f "$F"; mkfifo "$F"
timeout 240 "${Q[@]}" <"$F" >"$L" 2>&1 &
q=$!
exec 3>"$F"
for i in $(seq 1 200); do grep -q TZD-INIT "$L" && break; sleep 1; done
sleep 1
# One command per line, slowly: the console's input buffer is small.
while IFS= read -r c; do printf '%s\n' "$c" >&3; sleep 3; done <<'EOF'
echo TZD-SH-ECHO hello
uname -s
cd /proc ; pwd
cat /proc/version | wc -l
echo one > /tmp/f ; echo two >> /tmp/f ; cat < /tmp/f
nosuchcmd
ls / | grep -c bin
oob
oob heap
echo TZD-SH-AFTER-OOB
exit
EOF
wait $q || true
exec 3>&-
rm -f "$F"
fail=0
# Serial output ends lines with CR LF.
check() { if tr -d '\r' <"$L" | grep -aq -- "$2"; then echo "ok   $1"; else echo "FAIL $1: no '$2'"; fail=1; fi; }
check "init"        "TZD-INIT: init (TzD) started"
check "prompt"      "tzd# "
# The console echoes what is typed: output may share a line with the
# prompt or the echo, so the checks match anywhere in a line.
check "echo"        "^TZD-SH-ECHO hello"
check "uname"       "^Linux"
check "cd/pwd"      "\(^\|tzd# \)/proc\$"
check "pipe"        "\(^\|tzd# \)1\$"
check "redirect"    "^two"
check "not found"   "nosuchcmd: not found"
# oob makes pointer errors on purpose: the dump, then the shell goes on.
check "oob trap"    "oob.c:43: tzd: out-of-bounds pointer access"
check "oob dump"    "where  : 4 bytes past the end of the object"
check "oob heap"    "oob.c:30: tzd: out-of-bounds pointer access"
check "after oob"   "^TZD-SH-AFTER-OOB"
check "exit"        "TZD-POWEROFF"
if grep -aq "\[kc2d\]" "$L"; then echo "FAIL trap:"; grep -a "\[kc2d\]" "$L" | head -5; fail=1; fi
[ $fail = 0 ] && echo "userland: all ok" || { echo "userland: FAILED (log: $L)"; exit 1; }
