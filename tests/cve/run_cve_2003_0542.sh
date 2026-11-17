#!/bin/bash
# Acceptance demo for task #19: CVE-2003-0542 (Apache 1.3 < 1.3.29,
# mod_alias/mod_rewrite: a regex with more than 9 captures overflows the
# stack array regmatch_t regm[10] -- local privilege escalation).
#
# The 1.3.42 fix is reverted in mod_alias.c (nmatch back to re_nsub + 1).
# Sources: a copy of the baseline tree with tests/apache-patches/ applied,
# the same source tests/run_apache.sh builds.  The vulnerable copy is built
# twice:
#   baseline : plain GCC 14.4, every file compiled from the copy
#   managed  : cc1tzd, Managed C (work/apache-tzd objects, mod_alias replaced)
# and both servers get AliasMatch with 19 captures + a matching request.
# Expected: baseline writes past regm (crash or silent stack corruption);
# managed traps in the regex engine before the write ("tzd: out-of-bounds").
#
# Needs: work/apache built (its *.o list) and tests/run_apache.sh run with
# the current patches.
set -u
T=$(cd "$(dirname "$0")/.." && pwd); R=$T/..
B=$R/work/apache/src/apache_1.3.42/src
O=$R/work/cve-2003-0542
A=$O/src
PORT=${1:-18090}
G=$R/work/build-c/gcc
mkdir -p "$O/base-obj"

# Patched copy (same as run_apache.sh), then the vulnerable mod_alias.c.
rm -rf "$A" && mkdir -p "$A" && cp -a "$B/." "$A/" || exit 1
for p in "$T"/apache-patches/*.patch; do
  [ -f "$p" ] || continue
  patch -s -d "$A" -p1 < "$p" || { echo "FAIL patch $(basename "$p")"; exit 1; }
done
diff -rq -x '*.o' "$A" "$R/work/apache-tzd/src" >/dev/null ||
  { echo "work/apache-tzd is not built from the current patches: run tests/run_apache.sh"; exit 1; }
M=$A/modules/standard/mod_alias.c
sed -i 's/ap_regexec(p->regexp, r->uri, AP_MAX_REG_MATCH, regm, 0)/ap_regexec(p->regexp, r->uri, p->regexp->re_nsub + 1, regm, 0)/' "$M"
grep -q 're_nsub + 1, regm' "$M" || { echo "revert of the fix failed"; exit 1; }
INC="-I$A/os/unix -I$A/include -I$A/main -I$A/regex -DLINUX=22 -DUSE_HSREGEX -DNO_DL_NEEDED -DPOSIX_MISTAKE"
files=$(cd "$B" && find . -name '*.o' ! -name 'gen_*' | sed 's/\.o$/.c/;s|^\./||' | sort)
[ -n "$files" ] || { echo "no baseline objects in $B (build work/apache first)"; exit 1; }

# baseline: plain GCC, whole tree from the copy
for f in $files; do
  (cd "$A/$(dirname $f)" && "$R/work/apache/cc14.sh" -I. $INC -c "$(basename $f)" \
     -o "$O/base-obj/$(echo $f | tr / _ | sed 's/\.c$/.o/')") || { echo "FAIL baseline $f"; exit 1; }
done
"$G/xgcc" -B"$G/" -o "$O/httpd-base" "$O"/base-obj/*.o "$R/work/apache/siglist.o" -lm -lcrypt -lexpat || exit 1
# managed: the vulnerable mod_alias.c, the other objects from run_apache.sh
(cd "$A/modules/standard" && "$R/bin/tzd" --host -w -fgnu89-inline -include "$R/work/apache-tzd/shim-tzd.h" -I. $INC \
   -c mod_alias.c -o "$O/mod_alias_tzd.o") || exit 1
tzd_objs=$(ls "$R"/work/apache-tzd/obj/*.o | grep -v modules_standard_mod_alias.o)
"$G/xgcc" -B"$G/" -o "$O/httpd-tzd" $tzd_objs "$O/mod_alias_tzd.o" "$R/work/apache-tzd/siglist.o" \
  "$R/libc2d/build/libc2d.a" -lm -lcrypt -lexpat || exit 1

run() {   # $1 = binary, $2 = label
  local S=$O/srv-$2
  mkdir -p "$S/logs" "$S/htdocs"
  echo ok > "$S/htdocs/target"
  : > "$S/logs/error_log"
  cat > "$S/httpd.conf" <<CONF
ServerType standalone
ServerRoot $S
PidFile $S/logs/httpd.pid
ScoreBoardFile $S/logs/httpd.scoreboard
LockFile $S/logs/accept.lock
ErrorLog $S/logs/error_log
Port $PORT
Listen 127.0.0.1:$PORT
ServerName localhost
DocumentRoot $S/htdocs
TypesConfig /dev/null
ResourceConfig /dev/null
AccessConfig /dev/null
AliasMatch ^/(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)(m)(n)(o)(p)(q)(r)(s)\$ $S/htdocs/target
CONF
  "$1" -X -f "$S/httpd.conf" > "$S/logs/stdout" 2>&1 &
  local pid=$!
  sleep 1
  local code
  code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/abcdefghijklmnopqrs")
  sleep 1
  kill $pid 2>/dev/null; wait $pid 2>/dev/null; local rc=$?
  echo "== $2: HTTP=$code server-exit=$rc"
  grep -h -E "tzd:|libc2d:" "$S/logs/stdout" "$S/logs/error_log" | head -2 | sed 's/^/   /'
}
run "$O/httpd-base" base
run "$O/httpd-tzd" tzd
# fixed Managed C server (fix kept, same patches) for reference: same request is served
run "$R/work/apache-tzd/httpd" tzd-fixed
