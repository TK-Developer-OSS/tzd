#!/bin/bash
# MySQL 3.23.58 server (mysqld) for the PHP 3 mysql extension's tests.
# The server is C++ and TzD has no C++ front end yet (task #55), so it is
# built with the host gcc/g++, outside TzD.  The client library that PHP
# links (libmysql, C) is built with TzD by tests/mysql/build_client.sh.
#   Source: CentOS vault mysql-3.23.58-16.RHEL3.1.src.rpm (TK approved the
#   download 2026-11-14), unpacked into work/mysql/srpm; RHEL's patches
#   (security fixes included) and tests/mysql/server-patches are applied.
#   tests/mysql/build_server.sh  -> work/mysql/inst/libexec/mysqld
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
M=$R/work/mysql; S=$M/srpm; B=$M/server
[ -f "$S/mysql-3.23.58.tar.gz" ] || { echo "no source in $S"; exit 1; }
rm -rf "$B"; mkdir -p "$B"
tar xzf "$S/mysql-3.23.58.tar.gz" -C "$B" --strip-components=1
for n in 3.23.51-manfixes 3.23.54-libdir 3.23.58-hotcopy \
         3.23.58-setpermission 3.23.58-typo 3.23.58-symlink 3.23.58-dropdb \
         3.23.58-config 3.23.58-security 3.23.58-security2 3.23.58-testing; do
  patch -d "$B" -p1 -s -f <"$S/mysql-$n.patch"
done
patch -d "$B" -p1 -s -f <"$S/mysql-errno.patch"
for p in "$T"/server-patches/*.patch; do patch -d "$B" -p1 -s -f <"$p"; done
# The mysql command-line client wants termcap; only libtinfo is installed.
mkdir -p "$M/lib"; ln -sf /usr/lib/x86_64-linux-gnu/libtinfo.so.6 "$M/lib/libtermcap.so"
# Old code: no loop-to-libcall rewriting (strmov is stpcpy: it would call
# itself), no strict aliasing, keep null checks; no _FORTIFY_SOURCE (the
# redeclared stpcpy turns its fortify inline into a self-calling stpcpy).
OLD="-U_FORTIFY_SOURCE -fno-tree-loop-distribute-patterns -fno-strict-aliasing -fno-delete-null-pointer-checks"
W="-w -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-incompatible-pointer-types"
# configure's four g++ checks (gethostbyname_r style, accept's length type)
# fail on today's g++ for an unrelated reason (-Werror and the
# `void std::exit` its C++ probe leaves in confdefs.h), which picks the
# Solaris 5-argument gethostbyname_r: glibc's are given here.
export mysql_cv_gethost_style=other mysql_cv_gethostname_style=glibc2 \
  mysql_cv_gethostname_arg=char mysql_cv_btype_last_arg_accept=socklen_t
cd "$B"
LDFLAGS="-L$M/lib" CC=gcc CXX=g++ \
CFLAGS="-O1 -std=gnu89 -fcommon -D_GNU_SOURCE $OLD $W" \
CXXFLAGS="-O1 -std=gnu++98 -fpermissive -w -fno-exceptions -fno-rtti $OLD -fno-lifetime-dse" \
./configure --prefix="$M/inst" --with-readline --without-debug \
  --disable-shared --without-innodb --without-berkeley-db --without-bench \
  --with-named-thread-libs=-lpthread --with-unix-socket-path="$M/inst/mysql.sock" \
  --enable-local-infile --with-extra-charsets=complex >cfg.log 2>&1 ||
  { tail -5 cfg.log; exit 1; }
make -j"${TZD_JOBS:-2}" >make.log 2>&1 || { grep -E "error:|undefined reference|cannot find" make.log | head -20; exit 1; }
make install >install.log 2>&1 || { tail -20 install.log; exit 1; }
tail -c 1000000 make.log >make.log.t && mv make.log.t make.log
echo "mysqld: $M/inst/libexec/mysqld"
