#!/bin/bash
# Build the guest kernel with TzD (task #42): every 64-bit kernel .c file
# through cc1tzd in kernel mode (tests/kernel/tzd-cc.sh), linked with the
# kernel runtime kc2d (tests/kernel/kc2d/).
#   tests/kernel/build_tzd.sh       -> work/linux/build-tzdk/arch/x86/boot/bzImage
# The kernel source is not changed for the runtime: kc2d is compiled with
# the kernel's own flags and added to the link as one more library
# (KBUILD_VMLINUX_LIBS).  The build log keeps its last 2 MB (disk rule,
# TK 2026-11-12).
set -e
V=${KVER:-6.12.84}
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
W=$R/work/linux; G=$R/work/build-c/gcc; S=$W/sysroot
B=$W/build-tzdk; L=$W/build-tzdk.log
[ -d "$W/linux-$V" ] || { echo "no kernel source: run tests/kernel/build_kernel.sh"; exit 1; }
sh "$T/apply_patches.sh" "$W/linux-$V"
cd "$W/linux-$V"
# Kbuild does not notice a new cc1tzd or native.list (same command
# line): start over
# (keeping .config) when its checksum changed.
# TZD_MAKEFLAGS=-k collects every compile error in one run.
# TZD_REUSE=1 keeps the objects anyway (while chasing boot problems: remove
# the objects a compiler change affects by hand; not for a final build).
sum=$(cat "$G/cc1tzd" "$T/native.list" | md5sum | cut -d' ' -f1)
if [ -z "$TZD_REUSE" ] && [ -d "$B" ] && [ "$(cat "$B/.tzd-cc1tzd" 2>/dev/null)" != "$sum" ]; then
  [ -f "$B/.config" ] && cp "$B/.config" "$W/.config-tzdk"
  rm -rf "$B"; mkdir -p "$B"
  [ -f "$W/.config-tzdk" ] && mv "$W/.config-tzdk" "$B/.config"
fi
mkdir -p "$B"; echo "$sum" >"$B/.tzd-cc1tzd"
CC="$T/tzd-cc.sh"
HF=(HOSTCFLAGS="-I$S/usr/include" HOSTLDFLAGS="-L$S/usr/lib/x86_64-linux-gnu")
# Kernel stacks 1 MiB (TK 2026-11-14; patches/0003-tzd-stack-order.patch,
# while TzD frames are large): seen by C, assembly and the linker script.
HF+=(KCPPFLAGS="-DTZD_STACK_ORDER=6")
if [ ! -f "$B/.config" ]; then
  make O="$B" CC="$CC" tinyconfig >/dev/null
  scripts/kconfig/merge_config.sh -O "$B" -m "$B/.config" "$T/minimal.config" >/dev/null
  make O="$B" CC="$CC" olddefconfig >/dev/null
fi

# kc2d with the kernel's flags: taken from the command that compiled
# lib/ctype.c (plain kernel flags, no per-file extras), with plain GCC
# instead of cc1tzd.
make O="$B" CC="$CC" "${HF[@]}" lib/ctype.o >"$L" 2>&1 || { tail -c 4000 "$L"; exit 1; }
python3 -I - "$B/lib/.ctype.o.cmd" "$B/kc2d.args" <<'PY'
import shlex, sys
line = open(sys.argv[1]).readline().split(":=", 1)[1]
a = shlex.split(line)[1:]          # drop the compiler
out, skip = [], False
for x in a:
    if skip: skip = False; continue
    if x == "-o": skip = True; continue
    if x.startswith("-Wp,") or x.endswith("/lib/ctype.c") or "MODNAME" in x: continue
    out.append(x)
open(sys.argv[2], "w").write("\n".join(out + ["-DKBUILD_MODNAME=\"kc2d\"", "-D__KBUILD_MODNAME=kmod_kc2d"]) + "\n")
PY
(cd "$B" && xargs -d '\n' -a kc2d.args "$G/xgcc" -B"$G/" -o kc2d.o "$T/kc2d/kc2d.c") >>"$L" 2>&1 \
  || { tail -c 4000 "$L"; exit 1; }
# kc2d.a = the runtime + the sizes of symbols without a TzD definition
# and the stubs of missing.list (tzd_sizes.py; empty until vmlinux.o exists).
# Always from empty: values from an earlier run would be linked into
# vmlinux.o and hide the symbols tzd_sizes.py looks for (undefined ones).
: >"$B/tzd_sizes.S"
pack() {
  "$G/xgcc" -B"$G/" -c -o "$B/tzd_sizes.o" "$B/tzd_sizes.S"
  rm -f "$B/kc2d.a"; ar rcs "$B/kc2d.a" "$B/kc2d.o" "$B/tzd_sizes.o"
}
kmake() {
  # shellcheck disable=SC2086
  make O="$B" -j"${TZD_JOBS:-2}" $TZD_MAKEFLAGS CC="$CC" "${HF[@]}" \
       KBUILD_VMLINUX_LIBS="lib/lib.a arch/x86/lib/lib.a $B/kc2d.a" bzImage >>"$L" 2>&1
}
pack
set +e
kmake; rc=$?
# Stack frames over CONFIG_FRAME_WARN, kept apart (the relinks below and
# the trim at the end would drop them): "<bytes> <function> <file>".
grep -o "^[^ ]*: In function '[^']*'\|frame size of [0-9]* bytes" "$L" |
  awk '/In function/ {f=$0; next} {n=$4; sub(/: In function /, " ", f); print n, f}' |
  sort -rn | uniq | head -c 200000 >"$L.frames" || true
if [ -f "$B/vmlinux.o" ]; then
  # Two more links: sizes 0 to get the addresses, then the real sizes.
  vt=$B/init/version-timestamp.o
  python3 -I "$T/tzd_sizes.py" zero "$B/vmlinux.o" "$B/tzd_sizes.S" "$vt" && pack && kmake && \
  python3 -I "$T/tzd_sizes.py" real "$B/vmlinux.o" "$B/vmlinux" "$B/tzd_sizes.S" "$vt" && pack && kmake
  rc=$?
fi
set -e
# Every function the assembly defines or calls must be in native.list.
missing=$(comm -23 <("$T/gen_native_list.sh" "$B") <(grep -v '^#' "$T/native.list" | sort -u))
grep -E -B3 -A12 ': error:|undefined reference|internal compiler error|\*\*\*' "$L" | head -c 200000 >"$L.err" || true
tail -c 2000000 "$L" >"$L.t" && mv "$L.t" "$L"
if [ -n "$missing" ]; then
  echo "native.list is missing (regenerate with tests/kernel/gen_native_list.sh):"; echo "$missing"; exit 1
fi
[ $rc = 0 ] || { head -60 "$L.err"; echo "(errors: $L.err)"; exit 1; }
echo "kernel: $B/arch/x86/boot/bzImage"
