#!/bin/bash
# Vulnerability-pattern suite: the same C program built with TzD and with
# plain GCC, run on the host, and the outcomes compared.  See README.md.
#
#   tests/vulnpat/run.sh [pattern]     default: all NN_*.c
#
# Each case is built five ways:
#   tzd-bad     bin/tzd -O2                         the flaw
#   tzd-fixed   bin/tzd -O2 -DVP_FIXED              the corrected program
#   gcc-bad     xgcc -O0, no stack protector, no _FORTIFY_SOURCE
#   gcc-hard    xgcc -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2
#               (what a distribution build usually adds)
#   gcc-fixed   xgcc -O2 -DVP_FIXED
# Outcome of one run:
#   trap            exit 134 with a "tzd:" or "libc2d:" message (TzD's
#                   checks or its runtime library stopped it)
#   abort           exit 134 without one (glibc's or _FORTIFY_SOURCE's own check)
#   signal-N        killed by signal N (e.g. 11 = SIGSEGV)
#   ran:<effect>    reached END; <effect> = corrupted / leaked / confused
#   ran:no-effect   reached END and the flaw had no visible effect
#   build-error / timeout / exit-N
# Verdict for TzD on the flaw:
#   STOPPED (trap), STOPPED (crash) = signal, not a clean trap,
#   MITIGATED = ran without effect, NOT STOPPED = ran with an effect.
# The fixed builds must be ran:* (anything else is a false positive).
#
# Results: work/vulnpat/results.tsv and a table on stdout.  Every program
# is self-contained: no input, no network, no files; 10 s limit each.
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$(cd "$T/../.." && pwd)
G=$R/work/build-c/gcc
W=$R/work/vulnpat; B=$W/build
PAT=${1:-}
mkdir -p "$B"
: >"$W/results.tsv"

[ -x "$G/xgcc" ] || { echo "no xgcc in $G"; exit 1; }

cls() {     # rc stdout-file stderr-file
    local rc=$1 out=$2 err=$3 eff
    eff=$(sed -n 's/^EFFECT \([a-z]*\).*/\1/p' "$out" | head -1)
    if [ "$rc" = 0 ] && grep -q '^END' "$out"; then
        if [ -z "$eff" ] || [ "$eff" = none ]; then echo ran:no-effect
        else echo "ran:$eff"; fi
    elif [ "$rc" = 134 ] && grep -q -E '(^|[^a-z])(tzd|libc2d):' "$err"; then echo trap
    elif [ "$rc" = 134 ]; then echo abort
    elif [ "$rc" = 124 ]; then echo timeout
    elif [ "$rc" -gt 128 ]; then echo "signal-$((rc - 128))"
    else echo "exit-$rc"; fi
}

# build-and-run: name variant compiler-command...
one() {
    local n=$1 v=$2; shift 2
    local exe=$B/$n.$v
    if ! "$@" -o "$exe" >"$exe.build" 2>&1; then echo build-error; return; fi
    timeout 10 "$exe" </dev/null >"$exe.out" 2>"$exe.err"
    cls $? "$exe.out" "$exe.err"
}

verdict() {
    case $1 in
        trap) echo "STOPPED (trap)" ;;
        signal-*) echo "STOPPED (crash)" ;;
        ran:no-effect) echo "MITIGATED" ;;
        ran:*) echo "NOT STOPPED" ;;
        *) echo "? $1" ;;
    esac
}

fp() { case $1 in ran:*) echo ok ;; *) echo "FALSE-POSITIVE($1)" ;; esac; }

TZD=("$R/bin/tzd" -O2 -w -I"$T")
GCC=("$G/xgcc" -B"$G/" -w -I"$T")

printf '%-34s %-15s %-14s %-14s %-9s %-9s %s\n' \
    case tzd-bad gcc-bad gcc-hard tzd-fix gcc-fix "verdict (TzD)"
for src in "$T"/[0-9][0-9]_*.c; do
    n=$(basename "$src" .c)
    [ -n "$PAT" ] && case $n in *$PAT*) ;; *) continue ;; esac
    tb=$(one "$n" tb "${TZD[@]}" "$src")
    tf=$(one "$n" tf "${TZD[@]}" -DVP_FIXED "$src")
    gb=$(one "$n" gb "${GCC[@]}" -O0 -fno-stack-protector -U_FORTIFY_SOURCE "$src")
    gh=$(one "$n" gh "${GCC[@]}" -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2 "$src")
    gf=$(one "$n" gf "${GCC[@]}" -O2 -DVP_FIXED "$src")
    v=$(verdict "$tb")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$tb" "$gb" "$gh" "$(fp "$tf")" "$(fp "$gf")" "$v" \
        >>"$W/results.tsv"
    printf '%-34s %-15s %-14s %-14s %-9s %-9s %s\n' \
        "$n" "$tb" "$gb" "$gh" "$(fp "$tf")" "$(fp "$gf")" "$v"
done
echo
echo "(results: $W/results.tsv; build logs and outputs: $B/<case>.<tb|tf|gb|gh|gf>.{build,out,err})"
