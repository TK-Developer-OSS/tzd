#!/bin/bash
# The TzD coreutils (task #71) against the host's, on commands whose output
# does not depend on the time or the machine: same stdout, same exit
# status.  A TzD trap (status 134) is a failure.
#   tests/coreutils/compare.sh [BUILD-DIR]    (default work/coreutils/tzd)
set -u
export LC_ALL=C
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
B=${1:-$R/work/coreutils/tzd}/src
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
cd "$D" || exit 1
printf 'b 2\na 1\nc 3\na 1\n\nlast line without newline' >in.txt
printf 'x,y,z\n1,2,3\n4,5,6\n' >in.csv
mkdir -p tree/sub tree/other; printf hello >tree/sub/f1; printf world >tree/other/f2; : >tree/empty
ln -s sub/f1 tree/link
dd if=/dev/zero of=big bs=1k count=300 2>/dev/null
fail=0; n=0
t() {   # t NAME CMD ARGS... : the same command from the host and from TzD
  local name=$1 cmd=$2; shift 2
  local want got wrc grc
  want=$(if [ "$cmd" = ginstall ]; then install "$@"; else "$cmd" "$@"; fi 2>&1 <in.txt); wrc=$?
  got=$("$B/$cmd" "$@" 2>&1 <in.txt); grc=$?
  n=$((n + 1))
  if [ "$want" = "$got" ] && [ $wrc = $grc ]; then echo "ok   $name"
  else echo "FAIL $name (rc $wrc/$grc)"; diff <(echo "$want") <(echo "$got") | head -4; fail=$((fail + 1)); fi
}
t echo echo -n a "b  c" 'd\te'
t printf printf '%5s|%-5s|%05d|%x|%.2f\n' ab cd 42 255 3.14159
t seq seq 1 3 10
t seq-w seq -w 8 12
t cat cat -n in.txt
t cat-A cat -A in.txt
t head head -n 2 in.txt
t tail tail -n 2 in.txt
t tail-c tail -c 7 in.txt
t wc wc in.txt
t wc-m wc -lwmc in.txt
t sort sort in.txt
t sort-nu sort -u in.txt
t sort-k sort -k2,2nr in.txt
t sort-t sort -t, -k2 in.csv
t uniq-c uniq -c in.txt
t cut cut -d, -f1,3 in.csv
t cut-c cut -c1-3 in.txt
t tr tr a-z A-Z
t tr-d tr -d 'a-c'
t tr-s tr -s ' ' '_'
t paste paste -d: in.csv in.csv
t join join -t, in.csv in.csv
t comm comm in.txt in.txt
t fold fold -w 5 in.txt
t fmt fmt -w 10 in.txt
t nl nl in.txt
t tac tac in.txt
t rev-od od -An -tx1 -N16 in.txt
t expand expand in.txt
t unexpand unexpand -a in.txt
t basename basename /usr/lib/file.txt .txt
t dirname dirname /usr/lib/file.txt
t realpath realpath -m /a/b/../c
t readlink readlink tree/link
t ls-1 ls -1 tree
t ls-lR ls -lR tree
t ls-F ls -F tree
t ls-S ls -S tree/sub
t du du -a tree
t du-b du -b big
t stat stat -c '%n %s %F' tree/sub/f1
t md5sum md5sum in.txt big
t sha1sum sha1sum in.txt
t sha256sum sha256sum in.txt big
t sha512sum sha512sum in.txt
t b2sum b2sum in.txt
t cksum cksum in.txt big
t sum sum in.txt
t base64 base64 in.txt
t base32 base32 in.txt
t basenc basenc --base16 in.txt
t expr expr 2 '*' 21
t factor factor 360 97
t numfmt numfmt --to=iec 1048576
t date-fmt date -u -d @86400 +%Y-%m-%dT%H:%M:%S
t date-d date -u -d '2026-02-28 + 2 days' +%F
t test-f test -f in.txt
t test-d test -d tree
t true true
t false false
t env-i env -i X=1 printenv X
t dd dd if=big bs=100k count=1 status=none
t shuf-seed shuf --random-source=in.txt in.csv
t split-l split -l 2 in.txt -
t pr pr -t -w 20 in.txt
t ptx ptx in.txt
t tsort tsort in.csv
t install-d ginstall -d /tmp/cu-cmp-dir
t yes-head head -n 3 in.txt
t sleep sleep 0.01
t id-u id -u
t tty tty
t nproc nproc --all
t uname-s uname -s
t pwd pwd
t whoami whoami
t true2 true
rmdir /tmp/cu-cmp-dir 2>/dev/null
echo "=== $((n - fail))/$n same as the host's coreutils"
[ $fail = 0 ]
