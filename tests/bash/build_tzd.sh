#!/bin/bash
# GNU bash 5.3 (patch level 9, the last one before the 2026-04-27 freeze of
# task #59) built with TzD for the guest (task #70): configure runs with
# the project GCC, then every .c (bash, its bundled readline/termcap/glob
# libraries) is compiled by cc1tzd and linked statically with libc2d.
# The build-time generators (mkbuiltins, mksignames, ...) are host tools:
# CC_FOR_BUILD is the plain GCC.  Triage pass: `make -k` collects every
# compile error.
#   tests/bash/build_tzd.sh   -> work/bash/tzd/bash
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
G=$R/work/build-c/gcc; SRC=$R/work/bash/bash-5.3; B=$R/work/bash/tzd
[ -f "$SRC/patchlevel.h" ] || { echo "no bash source (work/bash/bash-5.3)"; exit 1; }
grep -q 'define PATCHLEVEL 9$' "$SRC/patchlevel.h" || { echo "bash source is not at patch level 9"; exit 1; }
cat >"$R/work/bash/cc-gcc.sh" <<CC
#!/bin/sh
exec $G/xgcc -B$G/ "\$@"
CC
cat >"$R/work/bash/cc-tzd.sh" <<CC
#!/bin/sh
exec $R/bin/tzd --host -w -include $T/tzd-bash.h "\$@" -U_FORTIFY_SOURCE
CC
chmod +x "$R/work/bash/cc-gcc.sh" "$R/work/bash/cc-tzd.sh"
rm -rf "$B"; cp -a "$SRC" "$B"; cd "$B"
# bash's own malloc is replaced by libc2d's (the TzD allocator with the
# two-stage release); no NLS; the bundled readline and termcap.
# bash_cv_getenv_redef=no: bash's own getenv/putenv/setenv/unsetenv would
# replace libc's, and libc calls getenv itself (_dl_non_dynamic_init at
# start-up) with the native calling convention -- a TzD function takes
# 24-byte pointers, so that traps at once.  No TzD function may stand in
# for a libc one that libc calls internally.
bash_cv_getenv_redef=no CC="$R/work/bash/cc-gcc.sh" ./configure --prefix=/usr \
  --without-bash-malloc --disable-nls --with-installed-readline=no \
  --enable-static-link --without-curses \
  >cfg-tzd.log 2>&1 || { tail -25 cfg-tzd.log; exit 1; }
make -k -j"${TZD_JOBS:-2}" CC="$R/work/bash/cc-tzd.sh" \
  CC_FOR_BUILD="$R/work/bash/cc-gcc.sh" >make-tzd.log 2>&1 || true
echo "=== compile errors (cc1tzd), by kind ==="
grep -E "error:|internal compiler error|sorry" make-tzd.log | \
  sed -E 's/^[^ ]+:[0-9]+:[0-9]+: //' | sort | uniq -c | sort -rn | head -25
echo "=== objects: $(find . -name '*.o' | wc -l); bash: $([ -x bash ] && echo linked || echo not linked)"
echo "undefined refs: $(grep -c 'undefined reference' make-tzd.log 2>/dev/null)"
