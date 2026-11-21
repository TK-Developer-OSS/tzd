# TzD

**Yet Another D language**

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

## Design under discussion: who may write

> Status: under discussion, not implemented yet.

TzD's current checks all ask whether a pointer may touch the memory (out of bounds, use after free, uninitialized). Some vulnerabilities write in-bounds, live, initialized memory from code that is not its rightful writer (for example CVE-2026-31431, "Copy Fail"); that is outside those checks. C source does not record who may write a piece of memory, so the compiler cannot guess it. The design adds a layer where a few annotations state it and the compiler enforces it.

- **Syntax**: annotations are comments, `// @tzd key=value ...` (`/* @tzd ... */` before an expression). Other compilers see plain comments. Annotations only add checks; an annotation with nothing to apply to, or an unknown key, is a compile error.
- **Write groups**: memory belongs automatically to a group named after the function that allocated it; that function is its gate. Code outside it may write only with an explicit `// @tzd set=allow group=<name>`. A pointer whose group cannot be traced is a compile error, like a missing prototype. Helpers such as memcpy are marked `// @tzd delegate` and write with their caller's permission. On free, the memory moves to the allocator's group, so the allocator needs no all-groups privilege. `mode=strict` is the default; `mode=free` explicitly lets anyone write.
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

## Cautions

This is a research environment. Do not treat it as proven safe.

- **Do not use it in production.** Linux, OpenSSH, bash and the rest built with TzD are not meant for the public internet. The guest's root password is `tzd` by default, and ports are forwarded to 127.0.0.1 on the host only. Do not expose them.
- **Nothing outside the bounds checks is protected.** TzD checks only code compiled by TzD. The host C library, the parts of libc2d written in plain C, the kernel's assembly, and places made exceptions by `#pragma tzd` are outside the checks. The exceptions are listed in `tests/*/tzd-*.h`.
- **Stopping is the normal outcome.** An out-of-range access, a use of freed memory or a double free stops the program (exit status 134). There is no continuing and no fallback.
- **It is slow.** About 20x the GCC build on pointer-heavy code (as of 2026-11-17).
- **It uses a lot of memory.** The guest starts with 2 GB, and the kernel's bounds table takes 512 MB. Addresses are never reused, so a long run uses up the virtual address space and the table and stops (a reboot fixes it).
- **Experimental:** `-ftzd-mpx` (checks with Intel MPX instructions) runs only on CPUs with MPX (Skylake to Comet Lake) and is not measured on real hardware. On a CPU without MPX the program stops at start-up.
- **SMP:** the kernel builds and boots on one or two CPUs. The kernel's bounds table has no locking for concurrent access from several CPUs yet.
- **Not supported:** stack traces of other tasks.
- **Work in progress:** see `STATUS.md` for the design and the state. It changes often.

## License

- `gcc-tzd/` (the GCC 14.4 front end): GPL-3.0-or-later ([COPYING](COPYING))
- `libc2d/` and `include/` (the runtime and the C library replacement): MIT ([LICENSE-MIT](LICENSE-MIT)). No conditions on programs built with TzD.
- Everything else (`bin/`, `tests/`, `docs/`, the specification): GPL-3.0-or-later

Other people's software (Linux, bash, coreutils, BusyBox, OpenSSH, OpenSSL) is not part of this repository.  The QEMU image in the releases contains binaries of them built with TzD; see SOURCES.md in the release.

## Special Thanks

Supporters who asked to be listed appear here.

## Acknowledgments

TzD is built on GCC, the GNU Compiler Collection, and works with glibc.  Thanks to the GNU project and everyone who has built and maintained GCC and glibc; TzD would not exist without their work.  Thanks also to Mr. Walter Bright, who created the D language, with respect.
