#!/bin/bash
# Juliet Test Suite for C/C++ 1.3 (NIST SARD) as TzD evidence (task #42).
#
# Each Juliet test case has a flawed function (bad) and correct ones
# (good).  Every selected case is built twice per compiler, bad only
# (-DOMITGOOD) and good only (-DOMITBAD), with TzD (bin/tzd --host) and
# with plain GCC, and run on the host:
#   TzD bad   caught (trap, exit 134) / missed (exit 0) / crash (signal)
#   TzD good  ok (exit 0) / FALSE POSITIVE (trap) / error
#   GCC       exit status or signal, for comparison (C lets the flaw run)
# Results: work/juliet/results.tsv and a summary per CWE.
#
#   tests/juliet/run.sh [pattern]     default pattern: _01.c (base variants)
# Data: work/juliet/juliet-1.3.zip, extracted to work/juliet/src (TK
# approved the download, 2026-11-14).  Cases that read sockets, files,
# the environment or stdin, and wide-char cases, are left out.
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
J=$R/work/juliet; S=$J/src/C; B=$J/build
G=$R/work/build-c/gcc
PAT=${1:-_01.c}
[ -d "$S/testcases" ] || { echo "no Juliet sources in $S"; exit 1; }
mkdir -p "$B"
CWES="CWE121 CWE122 CWE124 CWE126 CWE127 CWE415 CWE416 CWE457 CWE476 CWE590 CWE761"

# The support code (printLine and friends), once per compiler.
"$R/bin/tzd" --host -O2 -w -c -I"$S/testcasesupport" -o "$B/io_tzd.o" "$S/testcasesupport/io.c"
"$G/xgcc" -B"$G/" -O2 -w -c -I"$S/testcasesupport" -o "$B/io_gcc.o" "$S/testcasesupport/io.c"

LIST=$B/cases.txt; : >"$LIST"
for c in $CWES; do
  find "$S/testcases/${c}_"* -name "*$PAT" | sort |
    grep -v -E "socket|fgets|fscanf|_w32|wchar|_listen|_connect|environment|file_" >>"$LIST"
done
echo "cases: $(wc -l <"$LIST")"

# One case: build the 4 programs, run them, print a result line.
one() {
  f=$1; n=$(basename "$f" .c); d=$B/bin; mkdir -p "$d"
  inc=(-I"$S/testcasesupport" -DINCLUDEMAIN -w)
  r_tb=build r_tg=build r_gb=build r_gg=build
  if "$R/bin/tzd" --host -O2 "${inc[@]}" -DOMITGOOD -o "$d/$n.tb" "$f" "$B/io_tzd.o" -lm 2>/dev/null; then
    timeout 10 "$d/$n.tb" </dev/null >/dev/null 2>"$d/$n.tb.err"; r_tb=$?
  fi
  if "$R/bin/tzd" --host -O2 "${inc[@]}" -DOMITBAD -o "$d/$n.tg" "$f" "$B/io_tzd.o" -lm 2>/dev/null; then
    timeout 10 "$d/$n.tg" </dev/null >/dev/null 2>"$d/$n.tg.err"; r_tg=$?
  fi
  if "$G/xgcc" -B"$G/" -O0 "${inc[@]}" -DOMITGOOD -o "$d/$n.gb" "$f" "$B/io_gcc.o" -lm 2>/dev/null; then
    timeout 10 "$d/$n.gb" </dev/null >/dev/null 2>&1; r_gb=$?
  fi
  if "$G/xgcc" -B"$G/" -O0 "${inc[@]}" -DOMITBAD -o "$d/$n.gg" "$f" "$B/io_gcc.o" -lm 2>/dev/null; then
    timeout 10 "$d/$n.gg" </dev/null >/dev/null 2>&1; r_gg=$?
  fi
  cwe=${n%%_*}
  case $r_tb in 134) tb=caught ;; 0) tb=missed ;; build) tb=build-error ;; 124) tb=timeout ;; *) tb=crash-$r_tb ;; esac
  case $r_tg in 0) tg=ok ;; 134) tg=FALSE-POSITIVE ;; build) tg=build-error ;; *) tg=error-$r_tg ;; esac
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$cwe" "$n" "$tb" "$tg" "$r_gb" "$r_gg"
  rm -f "$d/$n.tb" "$d/$n.tg" "$d/$n.gb" "$d/$n.gg"
}
export -f one; export R S B G
xargs -a "$LIST" -P "$(nproc)" -I{} bash -c 'one "$1"' _ {} | sort >"$J/results.tsv"

# Summary per CWE: TzD on bad (caught / missed / other), TzD on good.
echo
printf '%-8s %5s %7s %7s %7s %9s %8s\n' CWE cases caught missed other good-ok good-bad
awk -F'\t' '{
  c[$1]++; if ($3=="caught") k[$1]++; else if ($3=="missed") m[$1]++; else o[$1]++;
  if ($4=="ok") g[$1]++; else gb[$1]++ }
  END { for (x in c) printf "%-8s %5d %7d %7d %7d %9d %8d\n", x, c[x], k[x], m[x], o[x], g[x], gb[x] }' \
  "$J/results.tsv" | sort
echo "(results: $J/results.tsv; columns: CWE case tzd-bad tzd-good gcc-bad-rc gcc-good-rc)"
