#!/bin/bash
# phpMyAdmin 2.2.6 as a PHP benchmark (task #54): the GCC and TzD builds
# of Apache 1.3 + mod_php3 run the same phpMyAdmin session against the
# MySQL 3.23 test server; the first pass compares the answers (they must be
# the same once the port and the clock are masked), then the session is
# repeated N times on each to time it.
#   tests/pma/bench.sh [N]          (default 20)
# Ports: 18090 (GCC), 18091 (TzD), MySQL 13306.
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
P=$R/work/php3; N=${1:-20}
[ -x "$P/apache-gcc/src/httpd" ] || bash "$R/tests/php3/build_gcc.sh" || exit 1
[ -x "$P/httpd-tzd" ] || bash "$R/tests/php3/build_tzd.sh" || exit 1
bash "$R/tests/mysql/server.sh" start || exit 1
start() {  # name httpd port
  local S=$P/pma-$1; rm -rf "$S"; mkdir -p "$S/htdocs" "$S/logs"
  bash "$T/install.sh" "$S/htdocs" 127.0.0.1:13306 "http://127.0.0.1:$3/pma/" || exit 1
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
MaxRequestsPerChild 0
CONF
  "$2" -f "$S/httpd.conf"
}
start gcc "$P/apache-gcc/src/httpd" 18090
start tzd "$P/httpd-tzd" 18091
sleep 2
q() { printf '%s' "$1" | sed 's/ /+/g; s/,/%2C/g; s/(/%28/g; s/)/%29/g; s/'"'"'/%27/g; s/=/%3D/g'; }
# One session: browse, create a table, fill it, query, dump, drop.
session() {  # port
  local u=http://127.0.0.1:$1/pma; local d="lang=en&server=1&db=tzd"
  local pages=(
    "index.php3"
    "left.php3?lang=en&server=1"
    "main.php3?lang=en&server=1"
    "db_details.php3?$d"
    "sql.php3?$d&sql_query=$(q "CREATE TABLE bench (id INT NOT NULL AUTO_INCREMENT PRIMARY KEY, name VARCHAR(40), score INT, note TEXT, KEY (score))")"
  )
  for i in $(seq 1 20); do
    pages+=("sql.php3?$d&table=bench&sql_query=$(q "INSERT INTO bench (name, score, note) VALUES ('user $i', $((i * 37 % 101)), 'note for row $i')")")
  done
  pages+=(
    "tbl_properties.php3?$d&table=bench"
    "sql.php3?$d&table=bench&pos=0&sql_query=$(q "SELECT * FROM bench")"
    "sql.php3?$d&table=bench&pos=0&sql_query=$(q "SELECT name, score FROM bench WHERE score > 50 ORDER BY score DESC")"
    "sql.php3?$d&table=bench&pos=0&sql_query=$(q "SELECT score % 10 AS d, COUNT(*), AVG(score) FROM bench GROUP BY d")"
    "tbl_select.php3?$d&table=bench"
    "tbl_printview.php3?$d&table=bench"
    "tbl_dump.php3?$d&table=bench&what=data"
    "db_printview.php3?$d"
    "db_stats.php3?lang=en&server=1"
    "sql.php3?$d&table=t&pos=0&sql_query=$(q "SELECT * FROM t")"
    "sql.php3?$d&is_js_confirmed=1&sql_query=$(q "DROP TABLE bench")"
    "db_details.php3?$d"
  )
  for p in "${pages[@]}"; do
    printf '=== %s\n' "${p%%\?*}"
    curl -s -m 30 -w '\n[%{http_code}]\n' "$u/$p"
  done
}
mask() { sed -E 's/1809[01]/180XX/g; s/[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:]{8}/DATE/g; s/(Generation Time|Generated|Date): .*/\1: X/; s/[A-Z][a-z]{2} [0-9]{1,2}, [0-9]{4} at [0-9:]+ [AP]M/DATE/g'; }
G=$(session 18090 | mask); Z=$(session 18091 | mask)
fail=0
n200=$(printf '%s\n' "$Z" | grep -c '^\[200\]'); npg=$(printf '%s\n' "$Z" | grep -c '^=== ')
echo "pages: $npg, HTTP 200: $n200"
[ "$n200" = "$npg" ] || fail=1
if [ "$G" = "$Z" ]; then echo "ok   same answers as GCC"; else
  echo "FAIL answers differ from GCC:"; diff <(printf '%s\n' "$G") <(printf '%s\n' "$Z") | head -20; fail=1; fi
if printf '%s' "$Z" | grep -q "Fatal error\|Parse error\|MySQL said"; then
  echo "FAIL PHP/MySQL errors:"; printf '%s\n' "$Z" | grep -B2 "Fatal error\|Parse error\|MySQL said" | head; fail=1; fi
# Timing: the session N times on each.
for s in gcc:18090 tzd:18091; do
  t0=$(date +%s.%N)
  for i in $(seq 1 "$N"); do session "${s#*:}" >/dev/null; done
  t1=$(date +%s.%N)
  awk -v n="$N" -v p="$npg" -v a="$t0" -v b="$t1" -v s="${s%:*}" \
    'BEGIN { t = b - a; printf "%-4s %d sessions x %d pages: %.2f s, %.1f ms/page\n", s, n, p, t, t * 1000 / (n * p) }'
done | tee "$P/pma-bench.txt"
for s in gcc tzd; do kill "$(cat "$P/pma-$s/logs/httpd.pid")" 2>/dev/null; done
bash "$R/tests/mysql/server.sh" stop
if grep -q "tzd:" "$P/pma-tzd/logs/error_log"; then
  echo "FAIL tzd trap:"; grep -A6 "tzd:" "$P/pma-tzd/logs/error_log" | head; fail=1
else echo "ok   no tzd trap"; fi
[ $fail = 0 ] && echo "pma: all ok" || echo "pma: FAILED"
exit $fail
