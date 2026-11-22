#!/bin/bash
# The MySQL 3.23 test server on the host (task #54): a fresh data
# directory in work/mysql/run, listening on 127.0.0.1:13306, with the
# database tzd (table t: (1,'alpha'), (2,'beta')) and the user tzd /
# password tzdpass, and admin / adminpass with every privilege (phpMyAdmin),
# from any host (QEMU guests come in as 127.0.0.1).
#   tests/mysql/server.sh start | stop
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
M=$R/work/mysql; D=$M/run
case ${1:-} in
start)
  [ -x "$M/inst/libexec/mysqld" ] || bash "$T/build_server.sh" || exit 1
  [ -f "$D/mysqld.pid" ] && kill "$(cat "$D/mysqld.pid")" 2>/dev/null && sleep 2
  rm -rf "$D"; mkdir -p "$D"
  "$M/inst/bin/mysql_install_db" --datadir="$D/var" >"$D/install.log" 2>&1 ||
    { tail -5 "$D/install.log"; exit 1; }
  "$M/inst/libexec/mysqld" --no-defaults --datadir="$D/var" --basedir="$M/inst" \
    --port=13306 --bind-address=127.0.0.1 --socket="$D/mysql.sock" \
    --pid-file="$D/mysqld.pid" --skip-name-resolve >"$D/mysqld.log" 2>&1 &
  for i in $(seq 1 30); do [ -S "$D/mysql.sock" ] && break; sleep 1; done
  "$M/inst/bin/mysql" --no-defaults -S "$D/mysql.sock" -u root -e "
    create database tzd; use tzd; create table t (id int, name char(20));
    insert into t values (1,'alpha'),(2,'beta');
    grant all on tzd.* to tzd@'%' identified by 'tzdpass';
    grant all privileges on *.* to admin@'%' identified by 'adminpass' with grant option;
    flush privileges;" ||
    { echo "mysql setup failed (log: $D/mysqld.log)"; exit 1; } ;;
stop)
  [ -f "$D/mysqld.pid" ] && kill "$(cat "$D/mysqld.pid")" 2>/dev/null; true ;;
*) echo "usage: $0 start|stop"; exit 2 ;;
esac
