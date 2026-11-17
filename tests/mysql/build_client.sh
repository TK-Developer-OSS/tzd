#!/bin/bash
# The MySQL 3.23.58 client library (libmysql: C) built with TzD, for the
# PHP 3 mysql extension (task #54).  configure runs with the host gcc (TzD
# rejects K&R test programs); then libmysql's sources (its own, plus the
# mysys/strings/dbug files it links in) are compiled by cc1tzd.
#   tests/mysql/build_client.sh   -> work/mysql/client-tzd/libmysql/.libs/libmysqlclient.a
# The server (C++) is tests/mysql/build_server.sh (g++, task #55).
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
M=$R/work/mysql; S=$M/srpm; B=$M/client-tzd
[ -f "$S/mysql-3.23.58.tar.gz" ] || { echo "no source in $S"; exit 1; }
rm -rf "$B"; mkdir -p "$B"
tar xzf "$S/mysql-3.23.58.tar.gz" -C "$B" --strip-components=1
for n in 3.23.51-manfixes 3.23.54-libdir 3.23.58-hotcopy \
         3.23.58-setpermission 3.23.58-typo 3.23.58-symlink 3.23.58-dropdb \
         3.23.58-config 3.23.58-security 3.23.58-security2 3.23.58-testing; do
  patch -d "$B" -p1 -s -f <"$S/mysql-$n.patch"
done
patch -d "$B" -p1 -s -f <"$S/mysql-errno.patch"
for p in "$T"/server-patches/*.patch "$T"/client-patches/*.patch; do
  [ -f "$p" ] && patch -d "$B" -p1 -s -f <"$p"
done
cat >"$M/cc-tzd.sh" <<CC
#!/bin/sh
exec $R/bin/tzd --host -w -fgnu89-inline -fcommon "\$@"
CC
chmod +x "$M/cc-tzd.sh"
mkdir -p "$M/lib"; ln -sf /usr/lib/x86_64-linux-gnu/libtinfo.so.6 "$M/lib/libtermcap.so"
# configure's four g++ checks (gethostbyname_r style, accept's length type)
# fail on today's g++ for an unrelated reason (-Werror and the
# `void std::exit` its C++ probe leaves in confdefs.h), which picks the
# Solaris 5-argument gethostbyname_r: glibc's are given here.
export mysql_cv_gethost_style=other mysql_cv_gethostname_style=glibc2 \
  mysql_cv_gethostname_arg=char mysql_cv_btype_last_arg_accept=socklen_t
cd "$B"
LDFLAGS="-L$M/lib" CC=gcc CFLAGS="-O1 -std=gnu89 -fcommon -D_GNU_SOURCE -w" ./configure --without-server \
  --without-readline --without-debug --disable-shared \
  --with-named-thread-libs=-lpthread --with-unix-socket-path="$M/inst/mysql.sock" \
  >cfg.log 2>&1 || { tail -5 cfg.log; exit 1; }
make -C include >make.log 2>&1
# The mysys/strings/dbug files libmysql compiles are symlinked first.
make -C libmysql link_sources >>make.log 2>&1
make -C libmysql -j"${TZD_JOBS:-2}" $MY_MAKEFLAGS CC="$M/cc-tzd.sh" CFLAGS="-O1 -D_GNU_SOURCE" \
  >>make.log 2>&1 || {
  grep -E -A4 "error:|internal compiler" make.log | head -60; echo "(log: $B/make.log)"; exit 1; }
echo "libmysqlclient (TzD): $B/libmysql/.libs/libmysqlclient.a"
