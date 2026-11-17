#!/bin/bash
# Real ssh use against the running guest (tests/userland/run_live.sh start,
# task #59): N interactive logins one after another, each with a pty, a
# few commands typed at the shell prompt, then a proper "exit".  Each step
# is timed.  Between logins the serial console's ps shows what is left,
# and the kc2d pool use (its 8 MiB reports) is picked out of the console
# log.
#   tests/userland/ssh_sessions.sh [N]      (default 5)
# Per-login transcripts: work/linux/poc/live/ssh_session_<i>.log
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
O=$R/work/linux/poc; LOG=$O/live/console.log; KEY=$O/ssh/guest_login
N=${1:-5}
X=$(mktemp); trap 'rm -f "$X"' EXIT
cat >"$X" <<'EXP'
# argv: key session-number
set key [lindex $argv 0]; set i [lindex $argv 1]
set timeout 300
set t0 [clock milliseconds]
proc mark {what} {
  global t0
  send_user "\n@@ [format %.1f [expr {([clock milliseconds] - $t0) / 1000.0}]]s $what\n"
}
proc fail {what} { mark "FAIL $what"; exit 1 }
spawn ssh -i $key -p 18022 -tt -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@127.0.0.1
expect {
  -re {(tzd|root@tzd:[^#]*)# $} { mark "prompt" }
  timeout { fail "no prompt" }
  eof { fail "eof before prompt" }
}
foreach c [list "echo TZD_SESSION_$i" id {ls /etc/ssh} ps {cat /proc/meminfo}] {
  send "$c\r"
  expect {
    -re {(tzd|root@tzd:[^#]*)# $} { mark "done: $c" }
    timeout { fail "timeout at: $c" }
    eof { fail "eof at: $c" }
  }
}
send "exit\r"
expect {
  eof { mark "logged out" }
  timeout { fail "no logout" }
}
exit 0
EXP
pool() {
  local h; h=$(tr -d '\r' <"$LOG" | grep -a '^\[kc2d\] pool ' | tail -1 | awk '{print $3}')
  echo "$((${h:-0} / 1048576)) MiB"
}
for i in $(seq 1 "$N"); do
  S=$O/live/ssh_session_$i.log
  echo "=== login $i (pool before: $(pool)) $(date +%T) ==="
  expect "$X" "$KEY" "$i" >"$S" 2>&1; rc=$?
  # The shell's output line, not the typed "echo ..." line (-x).
  tr -d '\r' <"$S" | grep -qx "TZD_SESSION_$i" && echo "  commands ran" || echo "  commands did NOT run"
  grep -a '^@@' "$S" | sed 's/^@@/ /'
  [ $rc = 0 ] || echo "  FAILED (expect exit $rc)"
  sleep 3
  echo "  left in the guest:"
  WAIT=3 bash "$T/run_live.sh" run 'ps' | grep -aE 'sshd|-sh' | sed 's/^/    /'
done
echo "=== pool after: $(pool) ==="
tr -d '\r' <"$LOG" | grep -acE '^Oops|tzd: ' | sed 's/^/Oops or TzD traps in the console log: /'
