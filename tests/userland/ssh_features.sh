#!/bin/bash
# What ssh is used for after the login, against the running guest
# (tests/userland/run_live.sh start, task #59).  Each check is its own
# connection, the way they are used: remote commands and their exit
# status, stdin/stdout through a pipe, scp both ways (SFTP protocol and
# the old one, -O) with a checksum, sftp batch, a local port forward to
# the guest's ftpd.  Each check is timed; PASS/FAIL per line.
#   tests/userland/ssh_features.sh
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
O=$R/work/linux/poc; KEY=$O/ssh/guest_login
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
OPT=(-F /dev/null -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
     -o LogLevel=ERROR -o BatchMode=yes)
SSH=(ssh "${OPT[@]}" -p 18022 root@127.0.0.1)
pass=0; fail=0
check() {  # name, then the command; PASS when it returns 0
  local name=$1 t0 rc; shift
  t0=$(date +%s.%N)
  "$@" >"$W/out" 2>&1; rc=$?
  if [ $rc = 0 ]; then pass=$((pass + 1)); r=PASS; else fail=$((fail + 1)); r=FAIL; fi
  printf '%-4s %6.1fs  %s\n' $r "$(echo "$(date +%s.%N) - $t0" | bc)" "$name"
  [ $rc = 0 ] || sed 's/^/        /' "$W/out" | tail -5
}
remote_cmd() { [ "$("${SSH[@]}" 'echo hello from; id')" = "hello from
uid=0(root) gid=0(root) groups=0(root)" ]; }
exit_status() { "${SSH[@]}" 'exit 7'; [ $? = 7 ]; }
pipe_io() {
  head -c 200000 /dev/urandom >"$W/p"
  "${SSH[@]}" 'cat > /tmp/pipe.bin' <"$W/p" &&
  "${SSH[@]}" 'cat /tmp/pipe.bin' >"$W/p2" && cmp "$W/p" "$W/p2"
}
head -c 1048576 /dev/urandom >"$W/f"
scp_up() { scp "${OPT[@]}" -P 18022 "$W/f" root@127.0.0.1:/tmp/f.sftp; }
scp_down() { scp "${OPT[@]}" -P 18022 root@127.0.0.1:/tmp/f.sftp "$W/f1" && cmp "$W/f" "$W/f1"; }
scp_old_up() { scp -O "${OPT[@]}" -P 18022 "$W/f" root@127.0.0.1:/tmp/f.old; }
scp_old_down() { scp -O "${OPT[@]}" -P 18022 root@127.0.0.1:/tmp/f.old "$W/f2" && cmp "$W/f" "$W/f2"; }
sftp_batch() {
  printf 'cd /tmp\nput %s f.batch\nls -l f.batch\nget f.batch %s\nrm f.batch\n' "$W/f" "$W/f3" >"$W/b"
  sftp "${OPT[@]}" -P 18022 -b "$W/b" root@127.0.0.1 && cmp "$W/f" "$W/f3"
}
forward() {
  # -L to the guest's ftpd (port 21): its banner comes back through ssh.
  "${SSH[@]}" -N -L 18121:127.0.0.1:21 & local p=$!
  local i; for i in $(seq 1 120); do
    (exec 3<>/dev/tcp/127.0.0.1/18121) 2>/dev/null && break; sleep 1
  done
  local b; b=$(timeout 20 bash -c 'exec 3<>/dev/tcp/127.0.0.1/18121; head -n 1 <&3')
  kill $p; wait $p 2>/dev/null
  echo "banner: $b"; case $b in 220*) return 0;; esac; return 1
}
echo "=== ssh features against the guest $(date +%T) ==="
check "remote command (echo; id)" remote_cmd
check "exit status passes back (exit 7)" exit_status
check "stdin/stdout pipe, 200 KB both ways" pipe_io
check "scp upload 1 MiB (SFTP protocol)" scp_up
check "scp download + compare" scp_down
check "scp -O upload 1 MiB (old protocol)" scp_old_up
check "scp -O download + compare" scp_old_down
check "sftp batch: put, ls, get, rm + compare" sftp_batch
check "local forward -L to the guest ftpd" forward
echo "=== $pass passed, $fail failed $(date +%T) ==="
