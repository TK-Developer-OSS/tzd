#!/usr/bin/env python3
"""Sampling profile of the running guest kernel (tests/userland/run_live.sh,
task #59: where the TzD kernel spends its time).

Through the QEMU monitor: stop the VM, read RIP/RBP/CS and walk the frame
pointer chain (the kernel is built with frame pointers), continue.  The
addresses are symbolized with vmlinux (booted nokaslr: no offset).

  tests/kernel/profile_guest.py SECONDS [RATE_HZ]   -> report on stdout
                                 (raw samples: work/linux/poc/live/prof.txt)
"""
import bisect, os, re, socket, subprocess, sys, time, collections

R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
MON = os.path.join(R, "work/linux/poc/live/monitor.sock")
VMLINUX = os.path.join(R, "work/linux/build-tzdk/vmlinux")
RAW = os.path.join(R, "work/linux/poc/live/prof.txt")
KBASE = 0xffffffff80000000
DEPTH = 16

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
rate = float(sys.argv[2]) if len(sys.argv) > 2 else 50

s = socket.socket(socket.AF_UNIX)
s.connect(MON)
buf = b""


def cmd(c):
    global buf
    if c is not None:
        s.sendall(c.encode() + b"\n")
    while b"(qemu) " not in buf:
        buf += s.recv(65536)
    out, buf = buf.split(b"(qemu) ", 1)
    return out.decode(errors="replace")


cmd(None)  # the banner and first prompt
reg = re.compile(r"\b(RIP|RBP|RSP|R11)=([0-9a-f]{16})")
cpl = re.compile(r"\bCPL=(\d)")
samples = []
r11s = []  # R11 per sample (tzd_sh_wipe keeps its size there)
t_end = time.time() + secs
while time.time() < t_end:
    cmd("stop")
    try:
        r = cmd("info registers")
        regs = dict(reg.findall(r))
        m = cpl.search(r)
        rip = int(regs.get("RIP", "0"), 16)
        chain = [rip]
        if m and m.group(1) == "0" and rip >= KBASE:
            bp = int(regs["RBP"], 16)
            for _ in range(DEPTH):
                if bp < KBASE >> 1:  # not a kernel address
                    break
                x = cmd("x /2gx 0x%x" % bp)
                v = re.findall(r"0x([0-9a-f]{16})", x.split(":", 1)[-1])
                if len(v) < 2:
                    break
                nbp, ret = int(v[0], 16), int(v[1], 16)
                if ret < KBASE:
                    break
                chain.append(ret)
                if nbp <= bp or nbp - bp > (1 << 20):
                    break
                bp = nbp
        else:
            chain = ["user"]
    finally:
        cmd("cont")
    samples.append(chain)
    r11s.append(regs.get("R11", "?"))
    time.sleep(1.0 / rate)

# Symbols.
syms = []
for line in subprocess.run(["nm", "-n", VMLINUX], capture_output=True,
                           text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]


def sym(a, off=False):
    if a == "user":
        return "[user]"
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0:
        return hex(a)
    return syms[i][1] + ("+0x%x" % (a - syms[i][0]) if off else "")


with open(RAW, "w") as f:
    for c, r11 in zip(samples, r11s):
        f.write("r11=%s " % r11 + " ".join([sym(c[0], True)] + [sym(a) for a in c[1:]]) + "\n")

n = len(samples)
self_ = collections.Counter(sym(c[0]) for c in samples)
# (the raw file has the sampled address as symbol+offset)
incl = collections.Counter()
for c in samples:
    for name in set(sym(a) for a in c):
        incl[name] += 1
print("samples: %d over %.0f s" % (n, secs))
print("\n-- self (where the CPU was) --")
for name, k in self_.most_common(30):
    print("%6.1f%%  %s" % (100.0 * k / n, name))
print("\n-- inclusive (on the stack) --")
for name, k in incl.most_common(45):
    print("%6.1f%%  %s" % (100.0 * k / n, name))
