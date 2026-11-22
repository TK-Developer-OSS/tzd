#!/bin/bash
# Apache 1.3.42 + mod_php3 (PHP 3.0.18) + the MySQL 3.23 client, all built
# with TzD (task #54).
#   PHP: configure runs with plain GCC (TzD rejects its K&R test programs,
#   e.g. `main(){return(0);}`), then every .c is compiled by cc1tzd
#   (bin/tzd --host).  Extensions as in tests/php3/build_gcc.sh.
#   Apache: the TzD objects of tests/run_apache.sh, with modules.c (adds
#   php3_module) and mod_php3.c compiled by TzD; PHP is built against the
#   same patched Apache headers (tests/apache-patches: scoreboard.h).
#   xml: Apache's expat-lite compiled by TzD (the host libexpat would be
#   native code behind TzD declarations).
#   mysql: tests/mysql/build_client.sh (libmysql by TzD).
#   tests/php3/build_tzd.sh   -> work/php3/httpd-tzd
# PHP_MAKEFLAGS=-k collects every compile error in one run.
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
P=$R/work/php3; MY=$R/work/mysql/inst; G=$R/work/build-c/gcc
AT=$R/work/apache-tzd
[ -x "$P/apache-gcc/src/httpd" ] || bash "$T/build_gcc.sh"
ls "$AT"/obj/*.o >/dev/null 2>&1 || bash "$R/tests/run_apache.sh" >/dev/null
MC=$R/work/mysql/client-tzd/libmysql/.libs/libmysqlclient.a
[ -f "$MC" ] || bash "$R/tests/mysql/build_client.sh"
cat >"$P/cc-tzd.sh" <<CC
#!/bin/sh
exec $R/bin/tzd --host -w -fgnu89-inline -fcommon "\$@"
CC
chmod +x "$P/cc-tzd.sh"
# The Apache tree PHP builds against: the GCC one (configured with
# php3_module) plus TzD's Apache patches.
A=$P/apache-tzd; rm -rf "$A"; cp -a "$P/apache-gcc" "$A"
for p in "$R"/tests/apache-patches/*.patch; do patch -s -d "$A/src" -p1 <"$p"; done
B=$P/tzd; rm -rf "$B"; cp -a "$P/php-3.0.18" "$B"
for p in "$T"/patches/*.patch; do patch -d "$B" -p1 -s -f <"$p"; done
cd "$B"
CC="$P/cc-gcc.sh" CFLAGS=-O2 ./configure --with-apache="$A" --with-ftp \
  --with-zlib --with-xml --with-mysql="$MY" --enable-track-vars \
  --enable-memory-limit --without-gd >cfg.log 2>&1 || { tail -5 cfg.log; exit 1; }
make -j"${TZD_JOBS:-2}" $PHP_MAKEFLAGS CC="$P/cc-tzd.sh" >make.log 2>&1 || {
  grep -E -A4 "error:|internal compiler" make.log | head -60; echo "(log: $B/make.log)"; exit 1; }
make install >install.log 2>&1
# Apache's side, compiled by TzD like tests/run_apache.sh.
O=$P/obj-tzd; rm -rf "$O"; mkdir -p "$O"
S=$A/src
AF=(-include "$AT/shim-tzd.h" -I"$S/os/unix" -I"$S/include" -I"$S/main"
    -I"$S/regex" -DLINUX=22 -DUSE_HSREGEX -DNO_DL_NEEDED -DPOSIX_MISTAKE)
cc() { "$P/cc-tzd.sh" -O2 "$@" 2>>"$O/err.txt" || { cat "$O/err.txt" | head -30; exit 1; }; }
cc "${AF[@]}" -c "$S/modules.c" -o "$O/modules.o"
cc "${AF[@]}" -I"$S/modules/php3" -c "$S/modules/php3/mod_php3.c" -o "$O/mod_php3.o"
for f in xmlparse xmltok xmlrole hashtable; do
  cc -I"$S/lib/expat-lite" -c "$S/lib/expat-lite/$f.c" -o "$O/expat_$f.o"
done
objs=$(ls "$AT"/obj/*.o | grep -v '/modules\.o$')
"$G/xgcc" -B"$G/" -o "$P/httpd-tzd" $objs "$O"/*.o "$AT/siglist.o" \
  "$B/libmodphp3.a" "$MC" "$R/libc2d/build/libc2d.a" \
  -lz -lm -lcrypt -lresolv 2>"$O/link.txt" || { head -30 "$O/link.txt"; exit 1; }
"$P/httpd-tzd" -l | grep -q mod_php3.c
echo "httpd (TzD + mod_php3): $P/httpd-tzd"
