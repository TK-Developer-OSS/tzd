# TzD

**Yet Another D language**

https://tk-developer-oss.github.io/tzd/

Seeing the reports of vulnerabilities that keep multiplying today, and the countermeasures and real damage that follow from them, I could not bring myself to do nothing.

Before ideals and identity, let us imagine the people who are suffering and being beaten down by this.

Searching for the options that are realistically open to us now is neither reckless nor sacrilegious.

This time I extended the C language, and built a compiler that accepts C syntax almost 100% as it is, but gives protection to the places that have become sources of instability, pointers above all. It accepts the loss of performance, and fails safe.

The D language (DMD) already exists. With respect for it and for Mr. Walter Bright, and also with the view that the name of a language is not anyone's property, I deliberately name this one Yet Another D Language, TzD, and send it out into the world.

Since being hard to tell apart is inconvenient, I write it as TzD for short.

---

## Support TzD

Reaching the current goal takes a very large amount of AI cost. To help release a finished version as soon as possible, please consider a donation. As a thank-you, supporters who ask in their Ko-fi message will be listed by name (or handle) in [Special Thanks](#special-thanks).

[![Support TzD on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/tk110835)

---

TzD is a C-compatible, GCC-based system programming language. It pays its respects to the D language and asks, in that spirit, what a "new D" should be: keep C's syntax and capabilities, and add the safety and abstraction facilities that C never had (Managed C, classes, contracts, delegates).

## Verification goals

1. **Apache HTTP Server 1.3** recompiled as Managed C (the first proof target). It is indexed first.
2. **Can a 2026 Linux system stop recent vulnerabilities?** The next index is a part that checks this on the software collected in the image: the Linux kernel 6.12, bash 5.3, GNU coreutils 9.11, BusyBox 1.36.1, OpenSSH 10.2p1 and OpenSSL 3.5.6, all built with TzD (exact versions in the release's SOURCES.md).
   - The question is whether these vulnerabilities can be covered by the compiler in a **general** way (bounds checks, use-after-free and double-free detection, wiping of dead memory), not by a fix specific to each one.
   - Recent vulnerabilities in the listed software will be followed from now on, and each is recorded as stopped, not stopped, or out of scope (outside the checks).
   - Status: the groundwork is done (the whole image builds and runs). No recent vulnerability has been verified yet.

- `.c` files compile as Managed C; `.d` files compile as TzD with the extensions. Both are ABI-compatible.
- Compile: `bin/tzd hello.d -o hello` (or `.c`; both go through cc1tzd). Tests: `tests/run_libd.sh`.
- Full specification: [SPEC.md](SPEC.md) (v0.1-draft)
- Status: research prototype. cc1tzd (a GCC 14.4 front end), libc2d, and TzD builds of Apache 1.3, bash, OpenSSH, coreutils and a Linux kernel run. See [STATUS.md](STATUS.md).
- Build: the tree holds only TzD's own sources; GCC 14.4.0 is built separately (see STATUS.md).

## Outlook

As a next step I want to bring in hardware support.  As the first one, Intel MPX (Memory Protection Extensions), now discontinued, can be tried out (`-ftzd-mpx`).  First make everything fail safe, whatever the cost; then see how far it can be made fast, and widen the PoC to the level of CPU logic and IP.

## Who may write

> Status: partly implemented (user space, variables with static storage; 2026-11-28). Kernel mode and heap memory are still under design.

TzD's other checks all ask whether a pointer may touch the memory (out of bounds, use after free, uninitialized). Some vulnerabilities write in-bounds, live, initialized memory from code that is not its rightful writer (for example CVE-2026-31431, "Copy Fail"); that is outside those checks. C source does not record who may write a piece of memory, so the compiler cannot guess it. A few annotations state it and the compiler enforces it.

- **Syntax** (implemented): annotations are comments, `// @tzd key=value ...`, right before the declaration they apply to. Other compilers see plain comments. Annotations only add checks; an annotation with nothing to apply to, or an unknown key, is a compile error.
- **Write groups** (implemented in user space): the declaration of the shared memory names its group and its writers.

  ```c
  // @tzd group=filecache writer=cache_fill,cache_update
  static struct page cached;
  ```

  Pointers to it are passed and read as in plain C. A store through a pointer by a function that is not listed stops the program, also when the function is reached through a pointer handed out long before; a library function such as memcpy writes with its caller's permission and is checked with the caller's name. A store that names the variable in a function that is not listed is a compile error. Memory without a group is not affected.
- **Kernel mode** (decided, not implemented): kernel mode is the strict mode for code with the freedom of bare metal. Handing memory to another part of the kernel needs a group annotation, and a missing one is a build error; an address whose group is unclear belongs to the local group of the code that declared it, and passing it out of that scope is an error. Code that uses real addresses (an integer made a pointer) compiles only in kernel mode.
- **Scope**: TzD closes the holes that C itself leaves open (bounds, lifetime, initialization, type confusion, the writer of shared memory, real addresses). Logic bugs, such as a wrong permission check, are outside it for now.
- **CVE-2026-31431 ("Copy Fail")**, status 2026-11-28: a protection aimed at countering it, write groups, has been implemented experimentally; it stops the same shape in user space (a shared cache handed out by reference and changed in place by a function that is not its writer). It is not verified against this CVE in the TzD-built kernel. That needs kernel-mode write groups on page-cache pages: the group kept per physical page, checks at calls into assembly, and a complete list of the page cache's writers. With them it is expected to stop, at the store by code that is not a page-cache writer, however the page reached it. The budget does not yet cover the cost of getting there; until then the TzD-built kernel does not stop it.

  Next, I hope to verify this more accurately and move on to the **Chasing Phase**: following the latest fixes in the listed software and recording, for each, whether TzD stops it.
- **Typed void pointers**: the annotation lists the role of each pointer level from the outside in; the count must match the declaration. A `void *` member of a struct needs such an annotation. Storing a typed pointer into a `void *` records a type id that is checked when it is cast back (the mechanism already used for function pointers).

  ```c
  // @tzd type=array,struct_a
  void **a;            /* a points to an array; each element points to a struct_a */

  struct s {
    // @tzd type=ptr,struct_a
    void *c;           /* a void * member must be annotated */
  };
  ```
- **Open points**: objects of different groups on one slab page, the gap between check and store on SMP, aliases of one physical page, uninstrumented code (assembly, native functions). DMA and read-side leaks are out of scope.

## CVE shapes: nine CVSS 9.8 vulnerabilities

> Status 2026-11-28. Small test programs, one per CVE: each reduces the CVE's faulty statement to a self-contained program of the same kind (the same check missing, the same arithmetic). They do not use the real Linux, OpenSSL or PHP, and the Linux ones run in user space, not in the kernel. The programs are not published.

| # | Software | CVE | Shape of the flaw | GCC -O0 | TzD |
|---|---|---|---|---|---|
| 20 | Linux (TIPC) | CVE-2021-43267 | key length taken from the message, record sized by the message | corrupted | **stopped** |
| 21 | Linux (cfg80211) | CVE-2019-17133 | element length byte copied into a 32-byte field | corrupted | **stopped** |
| 22 | Linux (ksmbd) | CVE-2022-47939 | connection freed by one request, used by the next | confused | **stopped** (use after free) |
| 23 | OpenSSL (MDC2) | CVE-2016-6303 | `num + len < BLOCK` wraps around | crash | **stopped** |
| 24 | OpenSSL (BN_bn2dec) | CVE-2016-2182 | unchecked error keeps a store loop running past its array | corrupted | **stopped** |
| 25 | OpenSSL (statem) | CVE-2016-6309 | pointer into a buffer kept across a moving realloc | corrupted | **stopped** (use after free) |
| 26 | PHP-FPM | CVE-2019-11043 | pointer computed before an empty string, inside an arena | corrupted | **not stopped** (gap) |
| 27 | PHP (wddx) | CVE-2016-5772 | name freed in a handler and again at cleanup | confused | **stopped** (double free) |
| 28 | PHP (phar) | CVE-2019-9021 | search from offset 1 with the full length reads past the name | leaked | **stopped** |

GCC -O0: plain GCC, no stack protector, no `_FORTIFY_SOURCE`. "Corrupted": other data was rewritten; "confused": two objects ended up sharing memory; "leaked": neighbouring data was read. In every case the corrected program also runs to the end under TzD (no false positive).

- **The gap (26):** PHP-FPM keeps request variables one after another in an arena, one malloc'd block. A pointer into it carries the bounds of the whole block, so a pointer a few bytes before one variable is still in bounds and writes the previous variable. TzD sees one object where the program has many. A way for an allocator to hand out pointers with narrower bounds is needed (open).

Target CVEs (all rated 9.8 by NVD, CVSS v3):

- Linux kernel: [CVE-2021-43267](https://nvd.nist.gov/vuln/detail/CVE-2021-43267) (TIPC MSG_CRYPTO key size not validated), [CVE-2019-17133](https://nvd.nist.gov/vuln/detail/CVE-2019-17133) (cfg80211 wext: long SSID not rejected), [CVE-2022-47939](https://nvd.nist.gov/vuln/detail/CVE-2022-47939) (ksmbd SMB2_TREE_DISCONNECT use after free)
- OpenSSL: [CVE-2016-6303](https://nvd.nist.gov/vuln/detail/CVE-2016-6303) (MDC2_Update integer overflow), [CVE-2016-2182](https://nvd.nist.gov/vuln/detail/CVE-2016-2182) (BN_bn2dec out-of-bounds write), [CVE-2016-6309](https://nvd.nist.gov/vuln/detail/CVE-2016-6309) (statem use after free after realloc)
- PHP: [CVE-2019-11043](https://nvd.nist.gov/vuln/detail/CVE-2019-11043) (PHP-FPM env_path_info underflow), [CVE-2016-5772](https://nvd.nist.gov/vuln/detail/CVE-2016-5772) (wddx double free), [CVE-2019-9021](https://nvd.nist.gov/vuln/detail/CVE-2019-9021) (phar heap over-read)

## Cautions

This is a research environment. Do not treat it as proven safe.

- **Do not use it in production.** Linux, OpenSSH, bash and the rest built with TzD are not meant for the public internet. The guest's root password is `tzd` by default, and ports are forwarded to 127.0.0.1 on the host only. Do not expose them.
- **Nothing outside the bounds checks is protected.** TzD checks only code compiled by TzD. The host C library, the parts of libc2d written in plain C, the kernel's assembly, and places made exceptions by `#pragma tzd` are outside the checks. The exceptions are listed in `tests/*/tzd-*.h`.
- **Stopping is the normal outcome.** An out-of-range access, a use of freed memory, a read of heap memory never written, a double free or a write by a function that is not a writer of the memory stops the program (exit status 134). There is no continuing and no fallback.
- **It is slow.** About 20x the GCC build on pointer-heavy code (as of 2026-11-17).
- **It uses a lot of memory.** The guest starts with 2 GB, and the kernel's bounds table takes 512 MB. Addresses are never reused, so a long run uses up the virtual address space and the table and stops (a reboot fixes it).
- **Experimental:** `-ftzd-mpx` (checks with Intel MPX instructions) runs only on CPUs with MPX (Skylake to Comet Lake) and is not measured on real hardware. On a CPU without MPX the program stops at start-up.
- **SMP:** the kernel builds and boots on one or two CPUs. The kernel's bounds table has no locking for concurrent access from several CPUs yet.
- **Not supported:** stack traces of other tasks.
- **Work in progress:** see `STATUS.md` for the design and the state. It changes often.

## Changelog

### 2026-11-28

- **Use after free and unwritten heap memory** (heap map): the heap is one region with two bits per 16-byte granule, whether it belongs to a live block and whether it was ever written. Reading or writing a freed block now stops the program (before, it ran on or crashed); reading heap memory that was never written stops it. memcpy, realloc and struct copies carry the bits instead of reading. Gap: the written bit is per granule, so an unwritten field that shares 16 bytes with a written one is not seen.
- **Pointers to ended locals**: a pointer to a local variable that is stored in memory is marked when the local's block ends, and its next load stops the program. Gap: such a pointer kept only in a register or in a local whose address is never taken is not seen.
- **Real addresses only in kernel mode**: outside `-ftzd-kernel`, converting an integer to an object pointer is a compile error. All-ones sentinels (`MAP_FAILED`), integers carried in a `void *` (thread arguments; any access through one still stops) and `sizeof`/`typeof` operands are allowed.
- **`// @tzd` annotations and write groups** (user space): see [Who may write](#who-may-write).
- **CVE shapes**: test programs for nine CVSS 9.8 CVEs in Linux, OpenSSL and PHP; TzD stops eight, one is a gap: see [CVE shapes](#cve-shapes-nine-cvss-98-vulnerabilities).
- **Tests**: `tests/managed` 193 of 195 pass (the two failures predate these changes); in the Juliet subset run by `tests/juliet/run.sh` no correct ("good") program is stopped, the use-after-free cases (CWE-416) are now all stopped by TzD, and more cases are stopped for CWE-457 (3 to 9), CWE-126 (10 to 14) and CWE-124 (16 to 17).

### 2026-11-22

- First public snapshot: cc1tzd (GCC 14.4 front end), libc2d, the TzD builds of Apache 1.3, bash, coreutils, BusyBox, OpenSSH, OpenSSL and the Linux kernel, and the QEMU image in the releases.

## License

- `gcc-tzd/` (the GCC 14.4 front end): GPL-3.0-or-later ([COPYING](COPYING))
- `libc2d/` and `include/` (the runtime and the C library replacement): MIT ([LICENSE-MIT](LICENSE-MIT)). No conditions on programs built with TzD.
- Everything else (`bin/`, `tests/`, `docs/`, the specification): GPL-3.0-or-later

Other people's software (Linux, bash, coreutils, BusyBox, OpenSSH, OpenSSL) is not part of this repository.  The QEMU image in the releases contains binaries of them built with TzD; see SOURCES.md in the release.

## Special Thanks

Supporters who asked to be listed appear here.

## Acknowledgments

TzD is built on GCC, the GNU Compiler Collection, and works with glibc.  Thanks to the GNU project and everyone who has built and maintained GCC and glibc; TzD would not exist without their work.  Thanks also to Mr. Walter Bright, who created the D language, with respect.
