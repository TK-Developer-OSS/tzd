# Do not give up on hardware bounds checking

*An appeal to Intel's architects, from the TzD project. November 2026.*

## What we did

TzD is a C compiler front end (on GCC 14.4) that gives every pointer its
bounds and stops the program at the first access outside them. No source
rewrite: with it we built a Linux 6.12 kernel (12 small patches), OpenSSH
10.2p1, bash 5.3 (no source change, four annotations) and busybox, booted
the result, and logged in over ssh. Three rules hold everywhere:

1. An access outside a pointer's bounds traps. Bounds cannot be forged by
   writing bytes.
2. Freed heap addresses are never handed out again.
3. Memory that is given up — freed blocks, dead locals, the whole stack
   frame at every return — is overwritten.

## What it costs in software

After removing every cost that protected nothing (ssh login went from
44 s to 0.3 s), this is what remains, on the same machine:

| Workload | GCC | TzD | Ratio |
|---|---|---|---|
| bash, empty loop, 20,000 iterations | 0.09 s | 1.78 s | 20x |
| bash, loop with array assignment | 0.19 s | 3.68 s | 19x |
| bash, 300 subshell calls | 0.12 s | 0.77 s | 6x |

There is no hot spot left to fix. One iteration of that empty loop makes
954 pointer loads and 400 pointer stores that each consult a side table,
and 310 wipes. The table costs 32 bytes per 8-byte pointer. This is the
price of keeping a pointer and its bounds in two different places, and
software cannot make it go away.

## What MPX got right, and why it died

MPX had the right two ideas: a check instruction, and a table keyed by
where the pointer is stored that rejects a pointer whose bytes were
changed. Our table is the same design, entry size included. We added an
MPX mode to TzD (`-ftzd-mpx`) and ran it on an emulated MPX processor.
What we hit is the list of reasons MPX failed:

- **Bounds live beside the pointer, not in it.** Four bound registers,
  reset by any unprefixed call or jump, and a compiler that must track
  them. GCC dropped that support in version 9; today we must rebuild the
  bounds before every single check.
- **Fail-open.** A table mismatch yields "everything allowed". On a CPU
  without MPX the instructions silently do nothing. A safety feature must
  fail closed; we had to add a start-up check that refuses to run.
- **No OS commitment.** Linux removed MPX in 5.6. It still resets the
  MPX state in signal handlers, so checks there do nothing.
- **Then the hardware left too.** A feature that ships for five years
  and disappears teaches every toolchain not to invest in the next one.

MPX did not fail because bounds checking is the wrong goal. It failed
because the pointer and its bounds were kept apart.

## What we ask for

- **Put the bounds in the pointer.** One architectural object, loaded,
  stored and passed atomically. CHERI has shown this works in silicon.
- **Fail closed.** No mode in which a check quietly becomes a no-op.
- **Check on the access itself**, not in separate instructions a
  compiler may forget or an attacker may skip.
- **Commit for a decade**, with the OS and compiler work done alongside.

The software side is ready. TzD's rules are a specification that such
hardware could enforce at a fraction of today's cost, and the code that
needs it — kernels, ssh servers, shells — already builds under them
unchanged. Most of the unauthorized access the world suffers still
starts with a pointer leaving its object. A 20x slowdown is what it
costs to stop that without you. Please do not leave it there.
