#!/bin/sh
# Build the guest kernel (task #42) with the project's GCC 14.4 (plain C,
# baseline before parts of it are compiled by cc1tzd).
#   tests/kernel/build_kernel.sh        -> work/linux/build-base/arch/x86/boot/bzImage
# (out-of-tree build, so cc1tzd builds can share the source tree)
# objtool needs libelf headers: Ubuntu's libelf-dev is unpacked into
# work/linux/sysroot (no system install).
set -e
V=${KVER:-6.12.84}
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
W=$R/work/linux; G=$R/work/build-c/gcc; S=$W/sysroot
mkdir -p "$W"; cd "$W"
if [ ! -f linux-$V.tar.xz ]; then
  curl -s -O https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$V.tar.xz
  curl -s -O https://cdn.kernel.org/pub/linux/kernel/v6.x/sha256sums.asc
fi
grep " linux-$V.tar.xz\$" sha256sums.asc | sha256sum -c - >/dev/null
[ -d linux-$V ] || tar xf linux-$V.tar.xz
sh "$T/apply_patches.sh" "$W/linux-$V"
if [ ! -f $S/usr/include/gelf.h ]; then
  mkdir -p debs $S; (cd debs && apt-get download libelf-dev >/dev/null)
  dpkg -x debs/libelf-dev_*.deb $S
  ln -sf /usr/lib/x86_64-linux-gnu/libelf.so.1 $S/usr/lib/x86_64-linux-gnu/libelf.so
fi
cd linux-$V
B=$W/build-base; mkdir -p "$B"
M="CC=$G/xgcc -B$G/"
make O="$B" "$M" tinyconfig >/dev/null
scripts/kconfig/merge_config.sh -O "$B" -m "$B/.config" "$T/minimal.config" >/dev/null
make O="$B" "$M" olddefconfig >/dev/null
make O="$B" -j"${TZD_JOBS:-2}" "$M" HOSTCFLAGS="-I$S/usr/include" \
     HOSTLDFLAGS="-L$S/usr/lib/x86_64-linux-gnu" bzImage >"$W/build.log" 2>&1 \
  || { tail -20 "$W/build.log"; exit 1; }
echo "kernel: $B/arch/x86/boot/bzImage"
