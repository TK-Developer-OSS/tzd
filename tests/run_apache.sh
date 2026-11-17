#!/bin/bash
# Build Apache 1.3.42 as Managed C with cc1tzd and smoke-test it.
#
#   tests/run_apache.sh [port]        (TZD_CFLAGS="-O2" for extra flags)
#
# Every source the baseline build compiles (work/apache, built with plain
# GCC 14.4) is compiled by `bin/tzd --host` (host system headers and libc
# through the raw-C boundary; provisional, task #14/#24), linked with
# libc2d.a, then started on 127.0.0.1:<port> (default 18080) with a minimal
# config.  The sources are a copy with tests/apache-patches/ applied.
# Checks: -v, -t, GET / (200 + body), GET /nope (404), directory index
# (mod_autoindex; libc2d qsort), /server-status with ExtendedStatus (other
# children's scoreboard slots), 8 requests to the preforked server, clean
# shutdown, and no tzd trap in the error log.
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/..
A=$R/work/apache/src/apache_1.3.42/src
O=$R/work/apache-tzd
PORT=${1:-18080}
TZD=$R/bin/tzd
mkdir -p "$O/obj" "$O/srv/htdocs/dir" "$O/srv/logs" "$O/srv/conf"
make -C "$R/libc2d" >/dev/null || exit 1

# Managed C sources: a copy of the baseline tree with tests/apache-patches/
# applied (TzD-side reimplementations, e.g. the scoreboard holds no
# pointers, task #33).  The baseline's *.o list still says what to build.
B=$A
A=$O/src
rm -rf "$A" && mkdir -p "$A" && cp -a "$B/." "$A/" || exit 1
for p in "$T"/apache-patches/*.patch; do
  [ -f "$p" ] || continue
  patch -s -d "$A" -p1 < "$p" || { echo "FAIL patch $(basename "$p")"; exit 1; }
done

# The host-side shim: _sys_siglist (glibc 2.32+ dropped it), raw C ABI.
printf '#pragma GCC system_header\nextern const char *_sys_siglist[];\n' > "$O/shim-tzd.h"
[ -f "$O/siglist.o" ] || gcc -c -o "$O/siglist.o" "$R/work/apache/siglist.c" || exit 1

fail=0
files=$(cd "$B" && find . -name '*.o' ! -name 'gen_*' | sed 's/\.o$/.c/;s|^\./||' | sort)
[ -n "$files" ] || { echo "no baseline objects in $A (build work/apache first)"; exit 1; }
n=0; bad=0
for f in $files; do
  n=$((n+1))
  if ! (cd "$A/$(dirname $f)" && "$TZD" --host ${TZD_CFLAGS:-} -w -fgnu89-inline -include "$O/shim-tzd.h" \
        -I. -I"$A/os/unix" -I"$A/include" -I"$A/main" -I"$A/regex" \
        -DLINUX=22 -DUSE_HSREGEX -DNO_DL_NEEDED -DPOSIX_MISTAKE \
        -c "$(basename $f)" -o "$O/obj/$(echo $f | tr / _ | sed 's/\.c$/.o/')" 2>"$O/obj/err.txt"); then
    echo "FAIL compile $f"; head -5 "$O/obj/err.txt"; bad=$((bad+1))
  fi
done
echo "compiled: $((n-bad)) / $n"
[ $bad = 0 ] || exit 1
G=$R/work/build-c/gcc
"$G/xgcc" -B"$G/" -o "$O/httpd" "$O"/obj/*.o "$O/siglist.o" "$R/libc2d/build/libc2d.a" \
  -lm -lcrypt -lexpat || exit 1

S=$O/srv
echo '<html><body>Hello from Managed C Apache</body></html>' > "$S/htdocs/index.html"
for f in b.txt a.txt c.html; do echo $f > "$S/htdocs/dir/$f"; done
cp "$B/../conf/mime.types" "$S/conf/" 2>/dev/null || : > "$S/conf/mime.types"
: > "$S/logs/error_log"; : > "$S/logs/access_log"
cat > "$S/conf/httpd.conf" <<CONF
ServerType standalone
ServerRoot $S
PidFile $S/logs/httpd.pid
ScoreBoardFile $S/logs/httpd.scoreboard
LockFile $S/logs/accept.lock
ErrorLog $S/logs/error_log
LogLevel info
Port $PORT
Listen 127.0.0.1:$PORT
ServerName localhost
DocumentRoot $S/htdocs
TypesConfig $S/conf/mime.types
DirectoryIndex index.html
LogFormat "%h %l %u %t \"%r\" %>s %b" common
CustomLog $S/logs/access_log common
ResourceConfig /dev/null
AccessConfig /dev/null
ExtendedStatus On
<Location /server-status>
SetHandler server-status
</Location>
CONF

check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2' want '$3'"; fail=1; fi; }
check "httpd -v" "$("$O/httpd" -v | head -1)" "Server version: Apache/1.3.42 (Unix)"
check "httpd -t" "$("$O/httpd" -t -f "$S/conf/httpd.conf" 2>&1)" "Syntax OK"
"$O/httpd" -f "$S/conf/httpd.conf" || { echo "FAIL start"; exit 1; }
sleep 2
U=http://127.0.0.1:$PORT
check "GET / status" "$(curl -s -m 5 -o /dev/null -w '%{http_code}' $U/)" 200
check "GET / body" "$(curl -s -m 5 $U/)" "<html><body>Hello from Managed C Apache</body></html>"
check "GET /nope" "$(curl -s -m 5 -o /dev/null -w '%{http_code}' $U/nope)" 404
check "autoindex order" "$(curl -s -m 5 $U/dir/ | grep -o '[abc]\.[a-z]*</A>' | tr -d '\n')" "a.txt</A>b.txt</A>c.html</A>"
# mod_status with ExtendedStatus reads other children's scoreboard slots
# (vhost by index, task #33): the VHost column names this server.
n=$(curl -s -m 5 $U/server-status | grep -c '<td nowrap>localhost')
check "server-status vhost" "$([ "${n:-0}" -ge 1 ] && echo yes)" yes
codes=""; for i in 1 2 3 4 5 6 7 8; do codes="$codes$(curl -s -m 5 -o /dev/null -w '%{http_code}' $U/) "; done
check "8 requests" "$codes" "200 200 200 200 200 200 200 200 "
kill "$(cat "$S/logs/httpd.pid")"; sleep 2
check "shutdown" "$(ps -eo args | grep -c "^$O/httpd")" 0
check "no tzd trap" "$(grep -c 'tzd:' "$S/logs/error_log")" 0
[ $fail = 0 ] && echo "apache: all ok" || echo "apache: FAILED"
exit $fail
