#!/bin/sh
# Boot the guest kernel in QEMU with a minimal initramfs and check it
# (task #42/#44).  Userland for now: the host's static busybox (to be
# replaced by init/bash/coreutils built with TzD).  Network: QEMU user
# mode (the guest is on the LAN side and may reach outside, for outward tests); only
# 127.0.0.1:$PORT on the host is forwarded to the guest's port 80.
#   tests/kernel/run_boot.sh [port]      (KERNEL=<bzImage> to boot another build;
#   MEM=<MiB> guest memory, default 256; TIMEOUT=<s>, default 90)
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
V=${KVER:-6.12.84}; W=$R/work/linux; PORT=${1:-18080}
K=${KERNEL:-$W/build-base/arch/x86/boot/bzImage}
[ -f "$K" ] || { echo "no kernel: run tests/kernel/build_kernel.sh"; exit 1; }
I=$W/initramfs; rm -rf "$I"; mkdir -p "$I/bin" "$I/dev" "$I/proc" "$I/sys" "$I/tmp" "$I/www"
cp /usr/bin/busybox "$I/bin/"
cp "$T/init.sh" "$I/init"; chmod +x "$I/init"
(cd "$I" && find . | cpio -o -H newc 2>/dev/null | gzip -9) >"$W/initramfs.cpio.gz"
L=$W/boot.log
timeout "${TIMEOUT:-90}" qemu-system-x86_64 -m "${MEM:-256}" -nographic -no-reboot -kernel "$K" \
  -initrd "$W/initramfs.cpio.gz" -append "console=ttyS0 panic=-1 nokaslr" \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:80 \
  -device virtio-net-pci,netdev=n0 </dev/null >"$L" 2>&1 &
Q=$!
for i in $(seq 1 "$(( ${TIMEOUT:-90} - 30 ))"); do grep -q TZD-NET-READY "$L" && break; sleep 1; done
body=$(curl -s -m 5 http://127.0.0.1:$PORT/)
wait $Q
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2' want '$3'"; fail=1; fi; }
check "boot"     "$(grep -c TZD-BOOT-OK "$L")" 1
check "eth0"     "$(grep -c 'inet 10.0.2.15/24' "$L")" 1
check "GET /"    "$body" "<html>hello from the guest</html>"
check "poweroff" "$(grep -c TZD-POWEROFF "$L")" 1
[ $fail = 0 ] && echo "kernel: all ok" || { echo "kernel: FAILED (log: $L)"; exit 1; }
