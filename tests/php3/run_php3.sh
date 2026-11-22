#!/bin/bash
# Apache 1.3 + mod_php3 + MySQL 3.23: the GCC build and the TzD build serve
# the same pages (tests/php3/pages) and must answer the same (task #54).
#   tests/php3/run_php3.sh          (builds what is missing)
# Ports: 18090 (GCC), 18091 (TzD), MySQL 13306 on 127.0.0.1.
# Checks: GET with query variables, the extensions (zlib, xml, ftp, posix,
# mysql), POST (urlencoded, 64 KiB field) and a multipart file upload;
# both builds give the same bytes; no tzd trap in the TzD error log.
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
P=$R/work/php3; M=$R/work/mysql
[ -x "$P/apache-gcc/src/httpd" ] || bash "$T/build_gcc.sh" || exit 1
[ -x "$P/httpd-tzd" ] || bash "$T/build_tzd.sh" || exit 1
# MySQL on the host: a fresh data directory, the test user and table.
bash "$R/tests/mysql/server.sh" start || exit 1
start() {  # name httpd port
  local S=$P/srv-$1; rm -rf "$S"; mkdir -p "$S/htdocs" "$S/logs"
  cp "$T"/pages/*.php3 "$S/htdocs/"; mkdir -p "$S/htdocs/tmp"
  cat >"$S/httpd.conf" <<CONF
ServerType standalone
ServerRoot $S
PidFile $S/logs/httpd.pid
ScoreBoardFile $S/logs/httpd.scoreboard
LockFile $S/logs/accept.lock
ErrorLog $S/logs/error_log
Port $3
Listen 127.0.0.1:$3
ServerName localhost
DocumentRoot $S/htdocs
TypesConfig $R/work/apache/src/apache_1.3.42/conf/mime.types
ResourceConfig /dev/null
AccessConfig /dev/null
AddType application/x-httpd-php3 .php3
CONF
  "$2" -f "$S/httpd.conf"
}
start gcc "$P/apache-gcc/src/httpd" 18090
start tzd "$P/httpd-tzd" 18091
sleep 2
U=$P/upload.txt; printf 'uploaded line\n' >"$U"
big=$(head -c 65536 /dev/zero | tr '\0' x)
req() {  # port -> all answers, separated
  local u=http://127.0.0.1:$1
  curl -s -m 20 "$u/basic.php3?q=hello&n2=42"; echo "--"
  curl -s -m 20 "$u/ext.php3"; echo "--"
  curl -s -m 20 -d "a=1&b=two+words&big=$big" "$u/post.php3"; echo "--"
  curl -s -m 20 -F a=x -F b=y -F "userfile=@$U" "$u/post.php3"; echo "--"
  curl -s -m 20 -o /dev/null -w '%{http_code}\n' "$u/nope.php3"
}
G=$(req 18090); Z=$(req 18091)
for s in gcc tzd; do kill "$(cat "$P/srv-$s/logs/httpd.pid")" 2>/dev/null; done
bash "$R/tests/mysql/server.sh" stop
fail=0
has() { if printf '%s' "$Z" | grep -q -- "$2"; then echo "ok   $1"; else echo "FAIL $1: no '$2'"; fail=1; fi; }
has "GET + variables"  "q=hello n=42"
has "string/array"     "^1,2,3$"
has "printf"           "00042|ab    |3.142|ff"
has "regex"            "a#b# aXc"
has "class"            "^class 2$"
has "zlib"             "^hello gz$"
has "xml"              "<A><B></B><C></C></A>"
has "mysql"            "^3=o'k$"
has "POST urlencoded"  "a=1 b=two words len=65536"
has "upload"           "upload upload.txt 14 uploaded line"
has "404"              "^404$"
if printf '%s' "$G$Z" | grep -q "Fatal error\|Warning\|Parse error"; then
  echo "FAIL PHP errors:"; printf '%s\n' "$Z" | grep "Fatal error\|Warning\|Parse error" | head -5; fail=1
else echo "ok   no PHP errors"; fi
if [ "$G" = "$Z" ]; then echo "ok   same answers as GCC"; else
  echo "FAIL answers differ from GCC:"; diff <(printf '%s\n' "$G") <(printf '%s\n' "$Z") | head -20; fail=1; fi
if grep -q "tzd:" "$P/srv-tzd/logs/error_log"; then
  echo "FAIL tzd trap:"; grep -A4 "tzd:" "$P/srv-tzd/logs/error_log" | head; fail=1
else echo "ok   no tzd trap"; fi
[ $fail = 0 ] && echo "php3: all ok" || echo "php3: FAILED"
exit $fail
