#!/bin/bash
# Kernel survey (task #42): build the guest kernel with survey-cc.sh (each
# .c through cc1tzd first) and summarize the first error of every file
# cc1tzd rejects.
#   tests/kernel/run_survey.sh            -> work/linux/survey/summary.txt
# Disk (TK 2026-11-12): errors are capped per file (survey-cc.sh); the
# previous survey and its build log are gzipped into work/linux/survey-archive
# (newest 5 kept), and the archive is trimmed from the oldest when it
# passes 1 GB.
set -e
V=${KVER:-6.12.84}
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
W=$R/work/linux; G=$R/work/build-c/gcc; S=$W/sysroot
SV=$W/survey; B=$W/build-tzd; A=$W/survey-archive
[ -d "$W/linux-$V" ] || { echo "no kernel source: run tests/kernel/build_kernel.sh"; exit 1; }

# Archive and rotate the previous survey.
mkdir -p "$A"
if [ -d "$SV" ] && [ -n "$(ls -A "$SV" 2>/dev/null)" ]; then
  stamp=$(date -r "$SV" +%Y%m%d-%H%M)
  tar -C "$W" -czf "$A/survey-$stamp.tar.gz" survey $( [ -f "$W/build-tzd.log" ] && echo build-tzd.log )
fi
ls -1t "$A"/survey-*.tar.gz 2>/dev/null | tail -n +6 | while read -r f; do rm -f "$f"; done
while [ "$(du -sb "$A" | cut -f1)" -gt 1073741824 ]; do
  old=$(ls -1t "$A"/survey-*.tar.gz 2>/dev/null | tail -1)
  [ -n "$old" ] || break
  rm -f "$old"
done
rm -rf "$SV" "$B"
mkdir -p "$SV/ok" "$SV/fail" "$B"

# Same configuration as the baseline (build_kernel.sh).
sh "$T/apply_patches.sh" "$W/linux-$V"
cd "$W/linux-$V"
M="CC=$G/xgcc -B$G/"
make O="$B" "$M" tinyconfig >/dev/null
scripts/kconfig/merge_config.sh -O "$B" -m "$B/.config" "$T/minimal.config" >/dev/null
make O="$B" "$M" olddefconfig >/dev/null

export TZD_SURVEY=$SV
set +e
make -k O="$B" -j"$(nproc)" CC="$T/survey-cc.sh" HOSTCFLAGS="-I$S/usr/include" \
     HOSTLDFLAGS="-L$S/usr/lib/x86_64-linux-gnu" bzImage >"$W/build-tzd.log" 2>&1
set -e
rm -f "$SV"/err.* "$SV"/rc.*

ok=$(ls "$SV/ok" | wc -l); fail=$(ls "$SV/fail" | wc -l); skip=$(ls "$SV/skip" 2>/dev/null | wc -l)
{
  echo "survey $(date '+%Y-%m-%d %H:%M'): ok=$ok fail=$fail skip(16/32-bit)=$skip"
  echo "pass rate: $(awk -v o="$ok" -v f="$fail" 'BEGIN { if (o + f) printf "%.1f%%", 100 * o / (o + f) }')"
  echo "--- first error of each rejected file, by message"
  for f in "$SV"/fail/*; do grep -m1 -E 'error:|sorry|internal compiler' "$f" || true; done \
    | sed -E 's/^.*(error|sorry, unimplemented|internal compiler error): //' \
    | sed -E "s/'[^']*'/Q/g" | sort | uniq -c | sort -rn | head -30
  echo "--- by location"
  for f in "$SV"/fail/*; do grep -m1 -E 'error:|sorry|internal compiler' "$f" || true; done \
    | sed -E "s|$W/linux-$V/||; s/:[0-9]+:[0-9]+: (error|sorry, unimplemented|internal compiler error): / /" \
    | sed -E "s/'[^']*'/Q/g" | sort | uniq -c | sort -rn | head -30
} >"$SV/summary.txt"
cat "$SV/summary.txt" | head -3
