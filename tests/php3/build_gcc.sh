#!/bin/bash
# Baseline: Apache 1.3.42 + mod_php3 (PHP 3.0.18) built with plain GCC 14.4,
# to compare against the TzD build (tests/php3/build_tzd.sh).
#   PHP extensions: ftp, zlib (host libz), xml (Apache's expat-lite), pcre,
#   posix, mysql (work/mysql/inst, tests/mysql/build_server.sh), plus the
#   standard functions.  No mbstring/openssl: PHP 3 has neither.
#   Source: work/php3/php-3.0.18 (TK approved the download 2026-11-14),
#   tests/php3/patches applied.
#   tests/php3/build_gcc.sh   -> work/php3/apache-gcc/src/httpd
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
P=$R/work/php3; G=$R/work/build-c/gcc; MY=$R/work/mysql/inst
[ -x "$MY/libexec/mysqld" ] || bash "$R/tests/mysql/build_server.sh"
cat >"$P/cc-gcc.sh" <<CC
#!/bin/sh
exec $G/xgcc -B$G/ -std=gnu89 -fcommon -fgnu89-inline -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-return-mismatch "\$@"
CC
chmod +x "$P/cc-gcc.sh"
A=$P/apache-gcc; rm -rf "$A"; cp -a "$R/work/apache/src/apache_1.3.42" "$A"
B=$P/gcc; rm -rf "$B"; cp -a "$P/php-3.0.18" "$B"
for p in "$T"/patches/*.patch; do patch -d "$B" -p1 -s -f <"$p"; done
cd "$B"
CC="$P/cc-gcc.sh" CFLAGS=-O2 ./configure --with-apache="$A" --with-ftp --with-zlib \
  --with-xml --with-mysql="$MY" --enable-track-vars --enable-memory-limit \
  --without-gd >cfg.log 2>&1 || { tail -5 cfg.log; exit 1; }
make -j"${TZD_JOBS:-2}" >make.log 2>&1 || { grep -E "error:|undefined" make.log | head; exit 1; }
make install >install.log 2>&1
cd "$A"
# Apache's configure needs bash (dash's echo breaks Configuration.apaci).
CC="$R/work/apache/cc14.sh" LIBS="$R/work/apache/siglist.o" bash ./configure \
  --with-layout=Apache --prefix="$P/inst-gcc" \
  --activate-module=src/modules/php3/libphp3.a >cfg.log 2>&1 || { tail -5 cfg.log; exit 1; }
# Only the server: the support tools (htpasswd, ...) do not build with
# today's glibc (getline) and are not needed.
make -C src -j"${TZD_JOBS:-2}" >make.log 2>&1 ||
  { grep -E "error:|undefined" make.log | head; exit 1; }
src/httpd -l | grep -q mod_php3.c
echo "httpd (GCC + mod_php3): $A/src/httpd"
