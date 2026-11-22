# TzD status and next steps (handoff)

Updated: 2026-11-07.  Read this first, then `task.md` (spec review backlog, 37 items), `tzd.md` (project rule file format, #36)
and `SPEC.md` (ChatGPT-drafted v0.1; only items TK marks 完了 in task.md are adopted).

## What exists
| Path | What |
|---|---|
| `work/src/gcc-14.4.0/` | GCC 14.4.0 source (pinned). `gcc/tzd` is a symlink to `gcc-tzd/`. |
| `work/build-c/` | Build tree (`--enable-languages=c,tzd`, no bootstrap). `make -j4` after edits; if the first make races on `gtype-tzd.h`/`options.h`, run make again. |
| `gcc-tzd/` | TzD front end. **cc1tzd no longer links GCC's `c/`** (only c-family/ for preprocessor, options, builtins). `tzd-lex.{h,cc}` lexer boundary (tokens carry decoded literal values), `tzd-parse.cc` own parser + type resolution + managed-pointer lowering to GENERIC, `tzd-decl.cc` scopes/builtins/GC node layout and the entry points c-family expects (C-parser-only ones report "not supported"), `tzd-lang.cc` hooks, `tzd-opts.cc` forked c-opts.cc, `tzd-tree.h`. |
| `libc2d/` | libc replacement around the host libc. `src/` = the runtime in hand-lowered C (`tzd_ptr {addr, base, size}`): bounds table, wipe, trap, register clear, syscall leaf, stdio subset, malloc/free/realloc, own `_start`, `main.c` (native main wrapping argv/envp, #25), `boundary.c` (`T **` marshalling to the host, #23). `tzd/` = ISO C parts written in **Managed C** and compiled by cc1tzd (string/memory and ctype written from the C standard; abs/labs/atoi/atol/strtol/strtoul; qsort; the printf family: one formatter, sprintf stops at the destination's bounds, `%n` traps, floating point still via the host snprintf); its share grows until libc2d becomes libd (#43). `apache-needed.md` = what Apache imports. |
| `bin/tzd` | Driver wrapper: `tzd x.d -o x`, `tzd x.c -o x` (both via cc1tzd), `--nolibc` links libc2d only, `--host` uses the host's system headers + libc through the raw-C boundary (#24). |
| `tests/cve/` | CVE acceptance demos (not published). |
| `tests/apache-patches/` | TzD-side changes to Apache applied by `run_apache.sh` to a copy of the source (scoreboard holds no pointers, #33; pool parts get their own bounds with `__tzd_narrow`, #13). |
| `tests/apache-pool/` | `run_pool.sh`: #13 demo on the real alloc.c (overflow into the next pool object: plain GCC corrupts it silently, Managed C traps). |
| `tests/kernel/` | Linux 6.12 guest (#42): `build_kernel.sh` (baseline by plain GCC 14.4, out of tree in `work/linux/build-base`), `run_boot.sh` (QEMU, busybox initramfs, user net with hostfwd 127.0.0.1:18080→80), `run_survey.sh` (every kernel .c through cc1tzd via `survey-cc.sh`, falls back to GCC; output capped, older surveys gzipped and rotated in `work/linux/survey-archive`), `patches/` (TzD-side kernel changes, applied once by `apply_patches.sh`: source the TzD rules require to be written differently). |
| `tests/` | `run_managed.sh` (Managed C and TzD through cc1tzd: `managed/ok_*` exit 0, `managed/trap_*` exit 134, `managed/err_*` must not compile; `.c` and `.d`; extra args pass to tzd, e.g. `--nolibc`, `-O2`), `run_host.sh` (`host/*.c` with `--host`: thunks, getopt/strtol marshalling), `run_apache.sh [port]` (**Apache 1.3.42 built as Managed C and smoke-tested**; `TZD_CFLAGS=-O2`), `run_libd.sh` (hand-lowered libc2d tests, compiled as plain C), `hello.c/.d`. |
| `work/apache/` | Apache 1.3.42 baseline built with plain GCC 14.4 (`cc14.sh`; needs `-fgnu89-inline` and a `_sys_siglist` shim). Its `*.o` list defines what `run_apache.sh` compiles. |
| `work/apache-tzd/` | Output of `tests/run_apache.sh`: Managed C objects, `httpd`, test server root. |

## Decisions so far (all from TK; see task.md history)
- Managed pointer is an object; all pointer ops go through it. Native pointer accessor is `pointer()`, internal only.
- User code passes pointer objects straight to puts etc.; libc is replaced by `libc2d` (bin/tzd links `libc2d/build/libc2d.a`).
- Separate from GCC's C front end at the lexer; fork GCC/c-family files into `gcc-tzd/` when they need to change.
- Argument `char` / `char[]` / `char *` become `string` (internally an alias) -- #20, scope of bare `char` unconfirmed.
- Test bed is Apache 1.3 only. POSIX parts are served by the host libc for now.
- `.c` = Managed C, `.d` = TzD, ABI compatible.

## Managed pointer lowering (what cc1tzd emits now)
- Representation (b) strict (task #1, implemented 2026-11-05, **adopted as the default 2026-11-06**: Managed C is
  implicitly strict — security is above compatibility, ambiguous code is a compile error fixed by code change or an
  explicit annotation, never by weakening a check): a pointer in memory is an 8-byte
  slot as in C (sizeof/offsets/layout/host ABI are C's); its bounds live in libc2d's shadow region keyed by the slot address
  (`libc2d/src/shadow.c`, 2-level table, MAP_NORESERVE). Values are the record `__tzd_ptr {T *addr; void *base; size_t size}`
  (= libc2d tzd_ptr); locals/params whose address is never taken stay records. Slot load = `tzd_sh_load` (traps unless the slot
  still holds the address its bounds were recorded for: rewriting pointer bytes cannot forge bounds, #29), store =
  `tzd_sh_store`, struct copy = `tzd_sh_copy`, static initializers -> section `tzd_shinit` registered before main.
  libc2d memcpy/memmove/realloc move entries, memset clears them, qsort is libc2d's `tzd_qsort`. Structs containing pointers
  passed/returned by value are lowered to hidden pointers with bounds copies (TK 2026-11-09 (b), provisional; the planned
  final rule is a compile error asking for TzD-style code, #46); public such functions get the symbol suffix `__tzd_byval`
  so a caller built without cc1tzd fails to link. Releasing memory (block exit for every local,
  parameter and compound literal, free/realloc, host blocks via malloc_usable_size, `__tzd_wipe` for allocators such as
  Apache pools) overwrites it twice (zero, then getrandom) and drops its entries (`tzd_sh_wipe`); a release whose size is
  unknown traps. Block exit also clears the registers (trial, #38/#39).
- Array decay / `&x` / string literal -> pointer object covering the whole object. Arithmetic moves addr only.
  `__tzd_narrow(p, n)` gives p the bounds [p, p+n) and traps unless they lie inside p's (can only narrow; #13).
  cc1tzd predefines `__TZD__` (and `__TZD_D__` for .d).
- Every `*p`, `p[i]`, `p->f` checks `[addr, addr+sizeof T)` inside `[base, base+size)`; failure calls `tzd_trap("file:line: tzd: ...")` (exit 134).
  Arrays inside structs decay with their own bounds (member overflow is caught before the next member).
  `&s.f` / `&p->f` cover only the member (task #21). Exceptions (provisional): a trailing `data[]`/`data[0]`/`data[1]`
  member decays to a pointer reaching the end of the enclosing allocation (flexible-array idiom); a struct-typed member
  at offset 0 keeps the parent's bounds (C-style inheritance, Apache casts `&ps.vbuff` back to the outer struct).
- `p - q` does not trap (an integer cannot corrupt memory; #21). Integer->pointer gives an object with no allocation.
  `((T *)0)->f` is an lvalue whose address is a constant (offsetof idiom); reading it traps. `__builtin_offsetof` works.
- Function pointers stay native. va_list stays native (parameters adjust to the raw `__va_list_tag *`).
- Raw-C boundary (`--host`, #24): declarations from system headers keep native pointers/layouts. Managed -> raw passes
  `pointer()` (addr). Raw -> managed has no default (task #1): host function results go through the rule table
  `host_ptr_rules` (string / object / opaque / inside an argument / mmap / shmat / NULL-terminated vector / ctype table);
  a host function returning a pointer without a rule is a compile error. Raw pointers in host memory (struct fields,
  variables) use the bounds user code recorded when it stored them, else the field's rule, else trap. `extern T x[]` takes
  its size from `__tzd_size.NAME` published by the defining unit (missing = link error). Host varargs get raw pointers;
  `T **` arguments are passed in place with their bounds re-registered (#23); functions converted to a function-pointer
  type of the other ABI go through a generated thunk (#22; a conversion that cannot get one -- function-pointer
  variables, varargs -- is a compile error). A host pointer of unknown length is a compile error everywhere,
  also inside system-header inline bodies (cc1tzd defines `__NO_INLINE__`, so glibc emits no inline bodies; #24). It also defines `__NO_CTYPE`: glibc's
  ctype table macros are off and isalpha()... are calls redirected to libc2d's Managed C versions (#43).
  User `main` is emitted as `__tzd_main`; libc2d's native `main` wraps argv/envp (#25).
  Host ISO C string/memory functions and malloc/calloc/realloc/free are redirected to libc2d's checked `tzd_*` versions
  (libc2d frees host-allocated "foreign" memory through the host's free); host `(buffer, length)` calls such as
  read/fgets/fread/snprintf/strftime check the length against the buffer's bounds first (#14, provisional tables in
  tzd-parse.cc: `libd_redirect_names`, `host_buf_rules`).
- `.d`: `string` = managed pointer to char, the same type as `char *` (#20, provisional); `bool`/`true`/`false`
  and `null` (the null pointer, not the integer 0) are keywords. `.d` is strict: implicit integer <-> pointer conversion
  is an error (in `.c` a warning, as in C). In `.c` these words stay ordinary identifiers.
- Fixed-width integer types `int8 ... uint64` are built in for both modes (#34).
- libc2d mode (no `--host`) has `include/` headers bound to libc2d's checked `tzd_*` functions: stdio, string, strings, stdlib
  (malloc/free/qsort/exit/abort/abs/atoi), ctype (out-of-range arguments trap), stddef, stdbool, stdint.
- `include/*.h` bind the ISO C functions libc2d implements to `tzd_*` via asm labels (libc2d mode).
- Supported since the Apache push: bit-fields, K&R definitions, anonymous struct/union members, statement expressions,
  `__asm__("a" "b")` labels, gnu89 `extern inline`, ++/-- on raw pointers.
- C99 designated initializers (`[N] =`, `.f =`, chains; later initializers replace earlier ones) and compound
  literals (`(T){...}`, block lifetime, wiped at block exit when they hold pointers) are supported. An initializer
  with too many elements, or an index outside the array, is a compile error (it used to write past the object, #37).
- Not supported yet (error/sorry): range designators, VLAs, asm statements, pragmas (skipped),
  GNU attributes: hints/diagnostics are skipped, `mode` is implemented, anything that
  can change layout or behavior (packed, aligned, cleanup, constructor, ...) is an error until implemented (#26);
  enums are plain int.

## Milestone (2026-11-05): Apache 1.3.42 runs as Managed C
All 53 Apache sources of the baseline build compile with cc1tzd (`--host`, -O0 and -O2), link with libc2d + host libc, and
the server answers: `-v`, `-t`, GET / 200, 404, mod_autoindex listing, 8 requests to the preforked server, clean shutdown,
no trap (`tests/run_apache.sh`).
#27 (sizeof(T *) was 24, hsregex mismatched 65-192 state regexes) is resolved by the (b) representation: sizeof(char *) == 8,
the Managed C hsregex matches 10-180 state patterns.

## Honest progress (estimate, not measured): about 55% toward the narrative goal
Done: environment, own front end for what Apache uses, representation (b) strict (8-byte pointers, bounds in the
shadow region, cannot be forged, wiped on release), managed pointers with bounds checks in all Apache code, host
boundary by rule table (no default bounds; unknown pointer-returning host functions are compile errors),
string/memory/malloc through checked libc2d, length checks on host buffer calls, Apache pool parts with their own bounds
(#13), attributes that change layout/behavior are no longer dropped (#26), C99 initializers (#37), one CVE demo + a pool
overflow demo. Not done: other host functions with buffers are unchecked (sprintf, sscanf, getpwnam_r, ...; the tables
are partial); temporal safety (#8: use after free / after ap_clear_pool); type keys (#28); more CVEs (#19); closures for
function-pointer variables (#22); annotations (#34 @pointer, #36 tzd.md); libc2d stream layer; TzD (.d) extensions
beyond string/bool/null (classes, contracts, delegates, exceptions).

## Next steps
1. TK decisions: adopt #1 (b) strict (implemented, uncommitted), #24 boundary policy, #21 exceptions. Then #8 temporal
   (lock-and-key) and #28 type keys on top of the shadow region.
2. #19: more Apache 1.3 memory-safety CVEs (re-introduce historic bugs) with tests/cve/ scripts.
3. #13: done for ap_palloc/ap_pvsprintf (pool-narrow.patch). Remaining: use after ap_clear_pool/ap_destroy_pool (#8).
4. #14: extend the redirect / buffer-length tables (sprintf family, sscanf, *_r functions).
3. #26 attributes, #22 function-pointer variables, remaining C features.
4. Open design items: task.md #1 (representation, `.c` implicit conversion), #2, #9, #13, #14, #24. libiberty license (LGPL) handling is undecided.

## Gotchas
- Never `rm` with a glob inside the project; use a fresh build directory instead.
- Apache's configure rejects CFLAGS with spaces: put flags in the `CC` wrapper.
- After changing `gcc-tzd/config-lang.in` (gtfiles), run `./config.status --recheck && ./config.status` in `work/build-c/gcc`,
  then `make s-gtype` before `make cc1tzd` (otherwise `gt-tzd-*.h` are missing / tzd-lang.o is stale).
- In C++ a namespace-scope `const` has internal linkage: tables referenced from other files need an `extern` declaration.
- libc2d and `tests/libd_*.c` are hand-lowered native C: compile them with cc1 (plain xgcc), never through cc1tzd.
- Adding a `static GTY(())` root in `gcc-tzd/*.cc`: run `make s-gtype` in `work/build-c/gcc` before `make cc1tzd`.
- `xgcc -x tzd` applies to every following file: put `-x none` before `.a`/`.o` inputs (bin/tzd does this).
- A parse error loop that consumes no token is cut by `PROGRESS_GUARD` (tzd-parse.cc); add it to new top-level loops.
- A pointer slot written by anything other than a pointer store (char/integer stores, host code, memset) traps on its next
  load (by design, #29). Host functions that move memory holding pointers need a libc2d version (like tzd_qsort).
