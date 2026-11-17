#!/bin/bash
# The TzD bash (task #70) as the login shell of the running guest
# (tests/userland/run_live.sh start): one interactive ssh login with a pty,
# then line editing and the usual interactive use, each step timed.
#   tests/userland/ssh_bash.sh        -> work/linux/poc/live/ssh_bash.log
# Speed is not judged here (TK 2026-11-16: "works, however slow").
set -u
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
O=$R/work/linux/poc; KEY=$O/ssh/guest_login; L=$O/live/ssh_bash.log
X=$(mktemp); trap 'rm -f "$X"' EXIT
cat >"$X" <<'EXP'
set key [lindex $argv 0]
set timeout 600
set t0 [clock milliseconds]
set fails 0
proc mark {what} {
  global t0
  send_user "\n@@ [format %7.1f [expr {([clock milliseconds] - $t0) / 1000.0}]]s $what\n"
}
# Send keys, wait for the expected output and then the prompt.
proc step {name keys want} {
  global fails
  send -- $keys
  expect {
    -re $want { }
    timeout { mark "FAIL $name (no output)"; incr fails; return }
    eof { mark "FAIL $name (eof)"; exit 1 }
  }
  expect {
    -re {root@tzd:[^#]*# $} { mark "PASS $name" }
    timeout { mark "FAIL $name (no prompt)"; incr fails }
    eof { mark "FAIL $name (eof)"; exit 1 }
  }
}
spawn ssh -i $key -p 18022 -tt -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@127.0.0.1
expect {
  -re {root@tzd:[^#]*# $} { mark "PASS login: bash prompt" }
  timeout { mark "FAIL no prompt"; exit 1 }
  eof { mark "FAIL eof before prompt"; exit 1 }
}
step "bash version"   "echo V=\$BASH_VERSION\r"           {V=5\.3\.9}
step "pipe + arith"   "echo \$((6*7)) | cat\r"             {\r\n42\r\n}
# Ctrl-A goes to the start of the line, Ctrl-E to the end.
step "Ctrl-A / Ctrl-E" "o AB\001ech\005C\r"                 {\r\nABC\r\n}
# Left arrow once and a character inserted there.
step "arrow keys"     "echo XZ\033\[DY\r"                  {\r\nXYZ\r\n}
# Up arrow recalls the last command.
step "history (up)"   "\033\[A\r"                          {\r\nXYZ\r\n}
step "history !!"     "!!\r"                               {\r\nXYZ\r\n}
# Tab completes a unique file name.
step "Tab completion" "cat /etc/nssw\t\r"                  {passwd: files}
step "ls | wc"        "ls /etc | wc -l\r"                  {\r\n *[0-9]+\r\n}
step "loop + func"    "f() { for i in 1 2 3; do printf %s \$i; done; echo; }; f\r" {\r\n123\r\n}
step "exit status"    "false; echo rc=\$?\r"               {rc=1}
step "Ctrl-C"         "sleep 100\r\003"                    {\^C}
send "exit 5\r"
expect eof
lassign [wait] pid spawnid os rc
if {$rc == 5} { mark "PASS exit 5 passes back" } else { mark "FAIL exit status $rc"; incr fails }
mark "fails: $fails"
exit [expr {$fails ? 1 : 0}]
EXP
[ -e "$O/initramfs.cpio.gz" ] || { echo "no image"; exit 1; }
expect "$X" "$KEY" >"$L" 2>&1; rc=$?
grep '^@@' "$L"
echo "transcript: $L"
exit $rc
