# Vulnerability patterns: GCC vs TzD

Twenty-eight test cases, small C programs, each the shape of a bug class that keeps coming
back in real CVEs (20–28: the shapes of nine CVEs rated 9.8 by NVD in the
Linux kernel, OpenSSL and PHP). The same source is built with plain GCC and with TzD,
and the outcomes are compared. The point: TzD stops these classes with
*general* checks (bounds, lifetime, initialization), not with a fix written
for each bug.

- **Deterministic.** No input, no race, no timing. The faulty statement
  runs on every execution. Where the GCC build shows an effect, the layout
  that causes it is fixed by the ABI or by glibc's allocator, so it is the
  same on every run (16 and 17 depend on stack layout; see below).
- **Effects, not attacks.** A program shows what the flaw does to data (a
  flag rewritten, a neighbour's bytes read, two objects confused) and
  stops there.
- **Bad and fixed.** `-DVP_FIXED` builds the corrected program. It must run
  to the end under every compiler, so the suite also checks TzD for false
  positives.

Run: `tests/vulnpat/run.sh [pattern]`. Results go to `work/vulnpat/results.tsv`.
The output protocol (`BEGIN` / `REACHED` / `EFFECT` / `END`) is described in
[vp.h](vp.h).

## Published test cases

A test case for a named CVE is published only when a proof of concept or
a concrete method for that CVE is already public. 22, 23 and 25 are left
out of the public tree for that reason; their rows stay in the table.
The test cases contain no attack: each stops at showing the effect on
data.

## Results

Measured on 2026-11-28 with the heap-map checks (task #8: heap
liveness, written bits, dangling stack slots) and, for row 19, the
write groups (task #79). No false positive: every fixed build
runs to the end under TzD and GCC.

| # | Case | CWE | Shape | GCC -O0 | GCC hardened | TzD |
|---|------|-----|-------|---------|--------------|-----|
| 01 | stack_member_strcpy | 121 | strcpy into a name field rewrites the next member | corrupted | abort (FORTIFY) | **trap** |
| 02 | heap_neighbor_overflow | 122 | length from the message, fixed-size buffer | corrupted | abort (FORTIFY) | **trap** |
| 03 | off_by_one_terminator | 193 | hand-written bounded copy writes `dst[cap] = 0` | corrupted | corrupted | **trap** |
| 04 | overread_length_field | 126 | echo reply trusts the claimed length (Heartbleed, CVE-2014-0160) | leaked | leaked | **trap** |
| 05 | int_overflow_alloc | 190, 680 | count × size wraps (OpenSSH, CVE-2002-0639) | corrupted | corrupted | **trap** |
| 06 | negative_index | 129, 124 | `if (idx < N)` lets a negative index through | corrupted | no effect¹ | **trap** |
| 07 | wrong_sizeof_copy | 805, 131 | `memcpy(dst, src, sizeof src)` | corrupted | corrupted | **trap** |
| 08 | unescape_overrun | 122 | trailing backslash skips the terminator (sudo, CVE-2021-3156) | corrupted | corrupted | **trap** |
| 09 | strncpy_unterminated | 170, 126 | strncpy leaves no terminator, strlen reads on | leaked | leaked | **trap** |
| 10 | type_confusion | 843 | the type tag comes from the message, not the object | leaked | leaked | **trap** |
| 11 | uaf_read | 416 | stale cached pointer reads the next session | confused | confused | **trap** (use of freed heap memory) |
| 12 | uaf_write | 416 | late callback writes a freed record | corrupted | no effect¹ | **trap** (use of freed heap memory) |
| 13 | uaf_after_realloc | 416 | pointer kept across a moving realloc | corrupted | no effect¹ | **trap** (use of freed heap memory) |
| 14 | double_free | 415 | error path frees, cleanup frees again | abort (glibc) | abort (glibc) | **trap** (libc2d) |
| 15 | double_free_after_reuse | 415, 416 | second free after the block was handed out again | confused | no effect¹ | **trap** (libc2d) |
| 16 | dangling_stack | 562 | address of a local handed out through an out parameter | confused | confused | **trap** (dangling slot) |
| 17 | uninit_pointer | 824, 457 | error ignored, out parameter never set | confused | no effect¹ | **trap** (null) |
| 18 | uninit_heap_leak | 908, 200 | malloc'd reply sent with old contents | leaked | no effect¹ | **trap** (never written) |
| 19 | control_unauthorized_write | — | in-bounds write by code that should not write the memory (Copy Fail class) | corrupted | corrupted | **trap** (not a writer of the group) |
| 20 | linux_tipc_key_len | 787 | key length from the message, record sized by the message (Linux, CVE-2021-43267) | corrupted | abort (FORTIFY) | **trap** |
| 21 | linux_ssid_len | 120 | element length byte copied into a 32-byte member (Linux, CVE-2019-17133) | corrupted | corrupted | **trap** |
| 22 | linux_ksmbd_tree_uaf | 416 | connection freed by one request, used by the next (Linux, CVE-2022-47939; not published) | confused | confused | **trap** (use of freed heap memory) |
| 23 | openssl_mdc2_update | 190, 787 | `num + len < BLOCK` wraps (OpenSSL, CVE-2016-6303; not published) | crash (SIGSEGV) | no effect¹ | **trap** |
| 24 | openssl_bn2dec_unchecked | 252, 787 | unchecked division error keeps a store loop going (OpenSSL, CVE-2016-2182) | corrupted | corrupted | **trap** |
| 25 | openssl_statem_realloc | 416 | message pointer kept across a moving realloc (OpenSSL, CVE-2016-6309; not published) | corrupted | no effect¹ | **trap** (use of freed heap memory) |
| 26 | php_fpm_path_underflow | 124 | pointer computed before an empty string, inside an arena (PHP-FPM, CVE-2019-11043) | corrupted | corrupted | **not stopped** (gap) |
| 27 | php_wddx_double_free | 415 | name freed in a handler and again at cleanup (PHP, CVE-2016-5772) | confused | no effect¹ | **trap** (libc2d) |
| 28 | php_phar_ext_overread | 125 | search from offset 1 with the full length (PHP, CVE-2019-9021) | leaked | leaked | **trap** |

¹ The -O2 build moved or removed the effect (for example, it drops a
malloc/free pair whose contents are never read). The flaw still executes;
this is not a protection.

## What the table says

- **27 of 28 stopped, no false positive.** Bounds (01–10), lifetime
  (11–16), initialization (17, 18) and the writer of shared memory (19)
  all end in a TzD trap, and so do eight of the nine CVE shapes (20–28);
  every fixed build runs to the end.
- **26 is a gap: sub-allocators.** PHP-FPM keeps request variables one
  after another in an arena, a single malloc'd block. A pointer into it
  carries the bounds of the whole block, so a pointer computed a few
  bytes before one variable is still inside the block and writes the
  previous variable. TzD sees one object where the program has many. A
  way for an allocator to hand out a pointer with narrower bounds is
  needed (open).
- **20–28 are shapes, not the CVEs.** Each reduces the CVE's faulty
  statement to a self-contained program of the same kind (same check
  missing, same arithmetic); it does not use the real software, and
  20–22 run in user space, not in the kernel.
- **How 11–13, 16 and 18 are stopped (task #8).** The heap is one region
  with two bits per 16-byte granule: whether it belongs to a live block,
  whether it was ever written. Every access through a heap pointer tests
  the first, a scalar read tests the second, stores set it; memcpy,
  realloc and struct copies carry the bits instead of reading. A pointer
  to a local stored in memory is noted, and when the local's block ends
  the slot is marked: its next load traps (16).
- **Known gaps.** Written bits are per granule: an unwritten field that
  shares 16 bytes with a written one is not seen. A pointer to a dead
  local kept only in a register or in a local variable whose address is
  never taken is not seen. The kernel (kc2d) does not have these checks.
- **19 needs one line of intent.** Bounds, lifetime and initialization
  checks do not cover a write that is legal by all three; C does not
  record who may write a piece of memory. The cache's declaration states
  it in a comment, `// @tzd group=filecache` (no function listed as a
  writer: the initializer sets the contents). Every store through a
  pointer, and every non-const pointer handed to a library function,
  tests whether the address lies in the grouped objects' section; inside
  it, the writing function must be listed (task #79). Other compilers
  see a comment, so the GCC columns do not change. 01–18 need no
  annotation.
- **GCC's warnings.** With `-Wall`, GCC warns at compile time on several
  of the bad programs (09, 10, 11, 13, 15, 16, 18), because each bug sits
  inside one small function. In real code the allocation, the free and the
  use are spread across functions and files, and these warnings do not
  fire; the built programs run on either way.
- **The hardened GCC column** (`-fstack-protector-strong
  -D_FORTIFY_SOURCE=2`) is what a distribution build usually adds. It
  catches libc calls whose destination size the compiler can see (01, 02);
  glibc catches a plain double free (14).
