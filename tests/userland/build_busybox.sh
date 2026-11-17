#!/bin/bash
# busybox 1.36.1 built with TzD (task #52): every .c through cc1tzd
# (bin/tzd --host), static.  The source is work/busybox/busybox-1.36.1
# (busybox.net tarball, TK approved the download 2026-11-14); the build
# directory is work/busybox/build.  Applets: tests/userland/busybox.config
# on top of allnoconfig.
#   tests/userland/build_busybox.sh      -> work/busybox/build/busybox
# BB_MAKEFLAGS=-k collects every compile error in one run.
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
S=$R/work/busybox/busybox-1.36.1; B=$R/work/busybox/build
G=$R/work/build-c/gcc
[ -d "$S" ] || { echo "no busybox source in $S"; exit 1; }
# TzD-side source changes (tests/userland/busybox-patches): each applied
# once; one that applies in neither direction stops the build.
for p in "$T"/busybox-patches/*.patch; do
  patch -d "$S" -p1 -R -s -f --dry-run <"$p" >/dev/null 2>&1 && continue
  patch -d "$S" -p1 -s -f <"$p" || { echo "patch failed: $p"; exit 1; }
done
# Kbuild does not notice a new cc1tzd or libc2d (same command line):
# start over when their checksum changed.
sum=$(cat "$G/cc1tzd" "$R/libc2d/build/libc2d.a" | md5sum | cut -d' ' -f1)
if [ -d "$B" ] && [ "$(cat "$B/.tzd-sum" 2>/dev/null)" != "$sum" ]; then
  rm -rf "$B"
fi
mkdir -p "$B"; echo "$sum" >"$B/.tzd-sum"
L=$B/build.log
# CC: TzD for C (kernel-style Kbuild calls it for .c and to link).
CCW=$B/tzd-cc.sh
cat >"$CCW" <<EOF
#!/bin/bash
exec "$R/bin/tzd" --host "\$@"
EOF
chmod +x "$CCW"
M=(make -C "$S" O="$B" CC="$CCW" HOSTCC="$G/xgcc -B$G/")
"${M[@]}" allnoconfig >"$L" 2>&1
# Our settings replace allnoconfig's (kconfig keeps the first line of a
# symbol, so they are removed before appending).
for k in $(sed -n 's/^\(# \)\?\(CONFIG_[A-Z0-9_]*\).*/\2/p' "$T/busybox.config"); do
  sed -i "/^$k=/d; /^# $k is not set/d" "$B/.config"
done
cat "$T/busybox.config" >>"$B/.config"
yes "" | "${M[@]}" oldconfig >>"$L" 2>&1
"${M[@]}" -j"$(nproc)" $BB_MAKEFLAGS busybox >>"$L" 2>&1 || {
  grep -E -B2 -A6 ": error:|internal compiler|undefined reference" "$L" | head -80
  echo "(log: $L)"; exit 1; }
tail -c 1000000 "$L" >"$L.t" && mv "$L.t" "$L"
echo "busybox: $B/busybox"
