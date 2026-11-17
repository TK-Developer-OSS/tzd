#!/bin/bash
# OpenSSL 3.5.6 built with TzD (task #59): Configure runs with the project
# GCC (TzD rejects some Configure probes), then every .c is compiled by
# cc1tzd.  Triage pass: `make -k` with CC=cc-tzd.sh to collect all compile
# errors in one run; produces work/openssl/tzd/libcrypto.a + libssl.a.
#   tests/openssl/build_tzd.sh   -> work/openssl/tzd/
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
G=$R/work/build-c/gcc; SRC=$R/work/openssl/openssl-3.5.6; B=$R/work/openssl/tzd
cat >"$R/work/openssl/cc-gcc.sh" <<CC
#!/bin/sh
exec $G/xgcc -B$G/ "\$@"
CC
cat >"$R/work/openssl/cc-tzd.sh" <<CC
#!/bin/sh
exec $R/bin/tzd --host -w -fcommon -include $T/tzd-openssl.h "\$@" -U_FORTIFY_SOURCE
CC
chmod +x "$R/work/openssl/cc-gcc.sh" "$R/work/openssl/cc-tzd.sh"
rm -rf "$B"; cp -a "$SRC" "$B"; cd "$B"
# The source tree holds the GCC baseline's build products (build_gcc.sh
# builds in-tree).  Remove every one from the copy: a copied .o is newer
# than its source, so make would keep it and the "TzD" library would be
# GCC objects (seen 2026-11-15: 1189 of 1219 objects were GCC's).
make distclean >/dev/null 2>&1 || true
find . -type f \( -name '*.o' -o -name '*.a' -o -name '*.d' -o -name '*.d.tmp' \) -delete
for pp in "$T"/patches/*.patch; do patch -p1 -s -f <"$pp"; done
CC="$R/work/openssl/cc-gcc.sh" ./Configure linux-x86_64 no-shared no-docs no-tests ${OSSL_EXTRA:-} \
  --prefix="$B/inst" >cfg-tzd.log 2>&1 || { tail -20 cfg-tzd.log; exit 1; }
# Compile the libraries with cc1tzd, keep going to collect every error.
make -k -j"${TZD_JOBS:-2}" CC="$R/work/openssl/cc-tzd.sh" \
  build_libs >make-tzd.log 2>&1 || true
echo "=== compile errors (cc1tzd) ==="
grep -E "error:|internal compiler error|sorry|trap" make-tzd.log | sort | uniq -c | sort -rn | head -40
echo "=== objects built: $(find . -name '*.o' | wc -l)" \
     "(cc1tzd compile commands: $(grep -c 'cc-tzd\.sh .* -c ' make-tzd.log)) ==="
ls -la libcrypto.a libssl.a 2>/dev/null || echo "(libs not produced yet)"
