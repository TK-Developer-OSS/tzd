#!/bin/bash
# The command-line PHP 3 (task #54): PHP 3.0.18 has no CLI SAPI (it came
# with PHP 4.3); its CGI binary run as `php -q script` was the command-line
# PHP of the era.  Same extensions and patches as tests/php3/build_tzd.sh,
# configured without Apache, compiled by TzD, linked statically for the
# guest's /usr/bin/php.  It is not served over HTTP (the web uses mod_php3).
#   tests/php3/build_cli.sh   -> work/php3/php-cli-tzd
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
P=$R/work/php3; MY=$R/work/mysql/inst; G=$R/work/build-c/gcc
MC=$R/work/mysql/client-tzd/libmysql/.libs/libmysqlclient.a
[ -x "$P/cc-tzd.sh" ] && [ -f "$MC" ] || bash "$T/build_tzd.sh" >/dev/null
B=$P/cli; rm -rf "$B"; cp -a "$P/php-3.0.18" "$B"
for p in "$T"/patches/*.patch; do patch -d "$B" -p1 -s -f <"$p"; done
cd "$B"
# xml: Apache's expat-lite headers where configure looks for an expat
# install (DIR/include/xml); the objects are build_tzd.sh's TzD ones.
mkdir -p expat/include/xml
cp "$P/apache-tzd/src/lib/expat-lite"/*.h expat/include/xml/
CC="$P/cc-gcc.sh" CFLAGS=-O2 ./configure --with-ftp --with-zlib --with-xml="$B/expat" \
  --with-mysql="$MY" --enable-track-vars --enable-memory-limit \
  --enable-force-cgi-redirect --without-gd >cfg.log 2>&1 || { tail -5 cfg.log; exit 1; }
# Objects by TzD; the link is ours: static, libc2d before the host
# libraries (like the guest's httpd, tests/userland/build_image.sh), the TzD
# libmysql and expat-lite.
cat >objs.mk <<'MK'
include Makefile
objs: $(OBJS) $(FUNCTIONS) pcrelib/libpcre.a regex/libregex.a
list:
	@echo $(OBJS) $(FUNCTIONS)
MK
make -j"${TZD_JOBS:-2}" $PHP_MAKEFLAGS -f objs.mk CC="$P/cc-tzd.sh" objs >make.log 2>&1 || {
  grep -E -A4 "error:|internal compiler" make.log | head -60; echo "(log: $B/make.log)"; exit 1; }
objs=$(make -s -f objs.mk list)
"$G/xgcc" -B"$G/" -static -o "$P/php-cli-tzd" $objs pcrelib/libpcre.a \
  regex/libregex.a "$P"/obj-tzd/expat_*.o "$MC" "$R/libc2d/build/libc2d.a" -lz -lm -lcrypt -lresolv \
  2>link.log || { head -30 link.log; exit 1; }
echo '<?php echo "ok ", 6*7, "\n"; ?>' >t.php3
[ "$("$P/php-cli-tzd" -q t.php3)" = "ok 42" ] || { echo "php-cli-tzd: smoke test failed"; exit 1; }
echo "php (TzD, static): $P/php-cli-tzd"
