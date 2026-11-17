// SPDX-License-Identifier: MIT
#include <d/stdlib.h>
#include "sys.h"
#include <d/shadow.h>

#define PAGE 4096UL

static tzd_size_t round_up(tzd_size_t n)
{
    return (n + PAGE - 1) & ~(PAGE - 1);
}

/* The live allocations: an open-addressing set of their start addresses.
   free() of an address not in it (a stack or static object, a block
   freed already) traps: CWE-590/761/415.  Not thread-safe yet (Apache
   1.3 is prefork).  */
#define REG_EMPTY 0UL
#define REG_GONE  1UL
static unsigned long *reg;
static tzd_size_t reg_cap, reg_used, reg_gone;

static tzd_size_t reg_slot(unsigned long a, tzd_size_t cap)
{
    return (tzd_size_t) ((a >> 12) * 0x9E3779B97F4A7C15UL) & (cap - 1);
}

static void reg_put(unsigned long a);

static void reg_grow(void)
{
    unsigned long *old = reg;
    tzd_size_t oldcap = reg_cap, i;
    tzd_size_t cap = reg_cap ? reg_cap * 2 : 1024;
    while ((reg_used + 1) * 2 > cap)
        cap *= 2;
    reg = (unsigned long *) tzd_sys_mmap(cap * sizeof *reg);
    if (!reg)
        tzd_trap("libc2d: out of memory for the allocation table\n");
    reg_cap = cap;
    reg_used = 0;
    reg_gone = 0;
    for (i = 0; i < oldcap; i++)
        if (old[i] > REG_GONE)
            reg_put(old[i]);
    if (old)
        tzd_sys_munmap(old, oldcap * sizeof *old);
}

static void reg_put(unsigned long a)
{
    tzd_size_t i;
    if ((reg_used + reg_gone + 1) * 2 > reg_cap)
        reg_grow();
    for (i = reg_slot(a, reg_cap); reg[i] > REG_GONE; i = (i + 1) & (reg_cap - 1))
        ;
    if (reg[i] == REG_GONE)
        reg_gone--;
    reg[i] = a;
    reg_used++;
}

/* Removes A; 0 when A is not a live allocation.  */
static int reg_take(unsigned long a)
{
    tzd_size_t i;
    if (!reg_cap)
        return 0;
    for (i = reg_slot(a, reg_cap); reg[i] != REG_EMPTY; i = (i + 1) & (reg_cap - 1))
        if (reg[i] == a) {
            reg[i] = REG_GONE;
            reg_used--;
            reg_gone++;
            return 1;
        }
    return 0;
}

/* Small blocks: fixed-size slots (task #66, TK 2026-11-16; a mapping per
   malloc made about 14,000 mmap calls in one ssh login).
   - One arena per size class, holding slots of that size only.  No
     variable-size carving, no coalescing.
   - Nothing is stored inside or next to a slot: which slots are live is a
     bitmap outside the arena, so no overflow or stale write can reach the
     allocator's own data.
   - The returned pointer's bounds are exactly the requested size.
   - An address is never used again.  Slots are handed out in address
     order; a freed slot is destroyed (zero, then meaningless bytes, the
     whole slot) and stays dead.  A stale pointer reaches destroyed bytes
     or an inaccessible page, never another object.
   - A run of slots (1 or 3 pages) whose slots are all freed goes back to
     the kernel and becomes inaccessible (PROT_NONE) for good.
   - Blocks over 2048 bytes keep their own mapping, as before.
   Not thread-safe yet, like the table above.  */
#define SL_NCLASS 14
#define SL_MAX 2048
#define SL_ARENA (1UL << 38)        /* address space of one class */
#define SL_OPEN_RUNS 16             /* runs made accessible at a time */
static const unsigned short sl_size[SL_NCLASS] = {
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048
};
static struct sl_arena {
    unsigned long start;            /* 0 until the class is first used */
    unsigned long bump;             /* slots handed out so far */
    unsigned long open_end;         /* bytes [0, open_end) were opened */
    unsigned long *live;            /* one bit per slot: 1 = live */
    unsigned short *cnt;            /* live slots of each run */
    unsigned long run_bytes, run_slots;
    unsigned long pend_lo, pend_hi; /* dead runs [lo, hi) not yet retired */
} sl_arena[SL_NCLASS];

static void sl_init(struct sl_arena *a, unsigned long size)
{
    /* A whole number of slots per run: 4096 for the powers of two,
       3 * 4096 for the sizes with a factor 3.  */
    a->run_bytes = (size % 3 == 0) ? 3 * PAGE : PAGE;
    a->run_slots = a->run_bytes / size;
    a->live = (unsigned long *) tzd_sys_mmap_noreserve(SL_ARENA / size / 8);
    a->cnt = (unsigned short *)
        tzd_sys_mmap_noreserve(SL_ARENA / a->run_bytes * sizeof *a->cnt);
    a->start = (unsigned long)
        tzd_sys_mmap_at(0, SL_ARENA, 0 /* PROT_NONE */, 0x4000);
    if (!a->live || !a->cnt || !a->start)
        tzd_trap("libc2d: out of address space for a malloc arena\n");
}

/* Dead runs -- all slots freed and destroyed, none will be handed out
   again -- go back to the kernel and become inaccessible for good: a
   fresh PROT_NONE mapping over them does both, and the table pages of
   their entries go back too.  Neighbouring dead runs are retired with one
   call: a dead run waits (destroyed, still mapped) until the next dead
   run is not its neighbour or SL_PEND_MAX of them have gathered.  */
#define SL_PEND_MAX 64
static void sl_flush(struct sl_arena *a)
{
    void *at = (void *) (a->start + a->pend_lo * a->run_bytes);
    unsigned long len = (a->pend_hi - a->pend_lo) * a->run_bytes;
    if (!len)
        return;
    if (tzd_sys_mmap_at(at, len, 0, 0x4010 /* FIXED|NORESERVE */) != at)
        tzd_trap("libc2d: cannot retire malloc runs\n");
    tzd_sh_release(at, len);
    a->pend_lo = a->pend_hi = 0;
}

static void sl_retire(struct sl_arena *a, unsigned long run)
{
    if (a->pend_lo == a->pend_hi) {
        a->pend_lo = run;
        a->pend_hi = run + 1;
    } else if (run == a->pend_hi)
        a->pend_hi++;
    else if (run + 1 == a->pend_lo)
        a->pend_lo--;
    else {
        sl_flush(a);
        a->pend_lo = run;
        a->pend_hi = run + 1;
    }
    if (a->pend_hi - a->pend_lo >= SL_PEND_MAX)
        sl_flush(a);
}

static void *sl_alloc(int c)
{
    struct sl_arena *a = &sl_arena[c];
    unsigned long size = sl_size[c], i, off, run;
    if (!a->start)
        sl_init(a, size);
    i = a->bump;
    off = i * size;
    if (off + size > SL_ARENA)
        tzd_trap("libc2d: a malloc arena is used up (addresses are never reused)\n");
    if (off + size > a->open_end) {
        unsigned long len = SL_OPEN_RUNS * a->run_bytes;
        if (a->open_end + len > SL_ARENA)
            len = SL_ARENA - a->open_end;
        if (tzd_sys_mprotect((void *) (a->start + a->open_end), len, 3) != 0)
            return 0;
        a->open_end += len;
    }
    run = off / a->run_bytes;
    a->live[i / 64] |= 1UL << (i % 64);
    a->cnt[run]++;
    a->bump = i + 1;
    return (void *) (a->start + off);
}

/* Frees the slot at BASE when it is one; 0 when BASE is in no arena.  */
static int sl_free(unsigned long base)
{
    int c;
    for (c = 0; c < SL_NCLASS; c++) {
        struct sl_arena *a = &sl_arena[c];
        unsigned long size = sl_size[c], off, i, run;
        if (!a->start || base - a->start >= SL_ARENA)
            continue;
        off = base - a->start;
        i = off / size;
        if (off % size != 0 || i >= a->bump
            || !(a->live[i / 64] & (1UL << (i % 64))))
            tzd_trap_ptr("libc2d: free of memory that is not a live allocation "
                         "(freed already, or not from malloc)\n",
                         (void *) base, (void *) base, size);
        a->live[i / 64] &= ~(1UL << (i % 64));
        tzd_sh_wipe((void *) base, size);   /* the whole slot */
        run = off / a->run_bytes;
        if (--a->cnt[run] == 0 && (run + 1) * a->run_slots <= a->bump)
            sl_retire(a, run);
        return 1;
    }
    return 0;
}

/* Large blocks (over SL_MAX): whole pages from one arena, in address
   order, an inaccessible page after each.  Like the slots, an address is
   never used again: a freed block is destroyed and replaced by a
   PROT_NONE mapping for good, so a stale pointer can never reach a later
   block (a plain munmap let the kernel hand the same address out again).  */
#define BIG_ARENA (1UL << 42)
static unsigned long big_start, big_next;

static void *big_alloc(tzd_size_t len)
{
    void *m;
    if (!big_start) {
        big_start = (unsigned long) tzd_sys_mmap_at(0, BIG_ARENA, 0, 0x4000);
        if (!big_start)
            tzd_trap("libc2d: out of address space for the large-block arena\n");
    }
    if (len > BIG_ARENA - big_next - PAGE)
        return 0;
    m = (void *) (big_start + big_next);
    if (tzd_sys_mprotect(m, len, 3) != 0)
        return 0;
    big_next += len + PAGE;           /* one page stays PROT_NONE */
    return m;
}

static void big_free(void *m, tzd_size_t len)
{
    if (tzd_sys_mmap_at(m, len, 0, 0x4010 /* FIXED|NORESERVE */) != m)
        tzd_trap("libc2d: cannot retire a large block\n");
    tzd_sh_release(m, len);
}

static int sl_class(tzd_size_t n)
{
    int c;
    for (c = 0; sl_size[c] < n; c++)
        ;
    return c;
}

/* One lock for the whole allocator (TK 2026-11-17: "never reused" has to
   hold against an attacker too).  Without it two threads in malloc at
   the same moment could be handed the same slot -- one address, two
   owners, which is exactly what never reusing an address rules out.  A
   spin lock: no thread library is assumed.  malloc or free called again
   from a signal handler while the lock is held does not return (malloc
   is not async-signal-safe); it never returns a shared slot.  */
static volatile int al_lock;
static void al_enter(void)
{
    while (__atomic_exchange_n(&al_lock, 1, __ATOMIC_ACQUIRE))
        __builtin_ia32_pause();
}
static void al_leave(void)
{
    __atomic_store_n(&al_lock, 0, __ATOMIC_RELEASE);
}

tzd_ptr tzd_malloc(tzd_size_t n)
{
    void *m;
    if (n == 0 || n > ((tzd_size_t) -1) - PAGE)
        return tzd_ptr_null();
    if (n <= SL_MAX) {
        al_enter();
        m = sl_alloc(sl_class(n));
        al_leave();
        if (!m)
            return tzd_ptr_null();
        return tzd_ptr_make(m, n);    /* bounds are exactly what was asked */
    }
    al_enter();
    m = big_alloc(round_up(n));
    if (m)
        reg_put((unsigned long) m);
    al_leave();
    if (!m)
        return tzd_ptr_null();
    return tzd_ptr_make(m, n);        /* bounds are exactly what was asked */
}

tzd_ptr tzd_calloc(tzd_size_t n, tzd_size_t sz)
{
    if (sz && n > ((tzd_size_t) -1) / sz)
        tzd_trap("libc2d: calloc size overflow\n");
    return tzd_malloc(n * sz);        /* fresh mappings are zero-filled */
}

/* With the host libc (tzd --host, task #24), memory allocated inside the
   host (strdup, getpwnam, ...) reaches Managed C as a "foreign" pointer:
   base == addr, size == PTRDIFF_MAX.  It goes back to the host allocator.
   Weak, so a libd-only program (no host libc) links without them.  */
extern void free(void *) __attribute__((weak));
extern void *realloc(void *, tzd_size_t) __attribute__((weak));
/* glibc: the usable size of a host block, so it can be wiped like ours
   before it goes back (TK 2026-11-06).  Without it the block is kept
   (never freed) rather than released unwiped (task #32).  */
extern tzd_size_t malloc_usable_size(void *) __attribute__((weak));
#define D_FOREIGN_SIZE ((tzd_size_t) (~(tzd_size_t) 0 >> 1))

static int foreign_p(tzd_ptr p)
{
    return p.size == D_FOREIGN_SIZE && p.addr == p.base;
}

void tzd_free(tzd_ptr p)
{
    if (p.base == 0)
        return;                     /* free(NULL) */
    if (foreign_p(p)) {
        if (!free)
            tzd_trap("libc2d: free of a foreign pointer without a host libc\n");
        if (!malloc_usable_size)
            return;                 /* kept: cannot wipe it (task #32) */
        tzd_sh_wipe(p.addr, malloc_usable_size(p.addr));
        free(p.addr);
        return;
    }
    if (p.addr != p.base)
        tzd_trap_ptr("libc2d: free of a pointer that is not the allocation start\n",
                     p.addr, p.base, p.size);
    al_enter();
    if (sl_free((unsigned long) p.base)) {
        al_leave();
        return;
    }
    if (!reg_take((unsigned long) p.base))
        tzd_trap_ptr("libc2d: free of memory that is not a live allocation "
                     "(freed already, or not from malloc)\n",
                     p.addr, p.base, p.size);
    /* The bytes are destroyed like any released memory (the mapping is
       then replaced, which also drops them; the wipe is the rule).  */
    tzd_sh_wipe(p.base, p.size);      /* overwrite, drop the bounds */
    big_free(p.base, round_up(p.size));
    al_leave();
}

/* aligned_alloc (C11): alignment up to a page.  Alignments up to 16 take
   the ordinary path (every slot starts 16-aligned); larger ones get a
   block of the large-block arena, which is page-aligned whatever its
   size.  A bigger alignment, or one that is not a power of two, is the
   fail-stop trap.  A size that is not a multiple of the alignment is
   undefined in C11 and accepted by glibc and gnulib's callers: the bounds
   are exactly the size asked for.  */
tzd_ptr tzd_aligned_alloc(tzd_size_t alignment, tzd_size_t n)
{
    void *m;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || alignment > PAGE)
        tzd_trap("libc2d: aligned_alloc: unsupported alignment\n");
    if (alignment <= 16)
        return tzd_malloc(n);
    if (n == 0 || n > ((tzd_size_t) -1) - PAGE)
        return tzd_ptr_null();
    al_enter();
    m = big_alloc(round_up(n));
    if (m)
        reg_put((unsigned long) m);
    al_leave();
    if (!m)
        return tzd_ptr_null();
    return tzd_ptr_make(m, n);
}

#include <d/string.h>

tzd_ptr tzd_realloc(tzd_ptr p, tzd_size_t n)
{
    tzd_ptr q;
    if (p.base == 0)
        return tzd_malloc(n);
    if (foreign_p(p)) {
        void *r;
        if (!realloc)
            tzd_trap("libc2d: realloc of a foreign pointer without a host libc\n");
        if (malloc_usable_size) {
            /* Move it into a libc2d block with exact bounds; the host block
               is wiped and freed like any other.  */
            tzd_size_t old = malloc_usable_size(p.addr);
            if (n == 0) {
                tzd_free(p);
                return tzd_ptr_null();
            }
            q = tzd_malloc(n);
            if (q.base == 0)
                return q;
            tzd_memmove(q, tzd_ptr_make(p.addr, old), old < n ? old : n);
            tzd_free(p);
            return q;
        }
        /* Old size unknown: the host would release the old block without
           it being wiped, so stop instead (TK 2026-11-07: every release
           is a two-stage destruction).  Task #3 (was #32).  */
        (void) r;
        tzd_trap("libc2d: realloc of a host block whose size is unknown "
               "(it cannot be wiped)\n");
    }
    if (n == 0) {
        tzd_free(p);
        return tzd_ptr_null();
    }
    q = tzd_malloc(n);
    if (q.base == 0)
        return q;                    /* old block stays valid */
    tzd_memmove(q, p, p.size < n ? p.size : n);
    tzd_free(p);
    return q;
}

/* reallocarray(ptr, n, sz): realloc to n*sz with an overflow check (BSD;
   OpenSSH uses it).  The managed block gets exactly n*sz bytes of bounds.  */
tzd_ptr tzd_reallocarray(tzd_ptr p, tzd_size_t n, tzd_size_t sz)
{
    if (sz && n > ((tzd_size_t) -1) / sz)
        tzd_trap("libc2d: reallocarray size overflow\n");
    return tzd_realloc(p, n * sz);
}

/* CPU_ALLOC / CPU_FREE (glibc sched.h): a bit set for N CPUs, in the
   allocator, so the set carries its size.  */
tzd_ptr tzd___sched_cpualloc(tzd_size_t n)
{
    return tzd_calloc(((n + 63) / 64) ? ((n + 63) / 64) : 1, 8);
}

void tzd___sched_cpufree(tzd_ptr p)
{
    tzd_free(p);
}

/* Process exit.  stdio writes are unbuffered, so nothing is pending.  */
void tzd_exit(int status)
{
    tzd_sys_exit(status);
}

void tzd_abort(void)
{
    tzd_trap("libc2d: abort\n");
}

/* abs and atoi are Managed C: libc2d/tzd/stdlib.c.  */

/* strdup/strndup through our allocator, so free() knows the block (a
   host strdup's block would be foreign to it).  */
tzd_ptr tzd_strdup(tzd_ptr s)
{
    tzd_size_t n = tzd_strlen(s) + 1;
    tzd_ptr q = tzd_malloc(n);
    if (q.base)
        tzd_memcpy(q, s, n);
    return q;
}

tzd_ptr tzd_strndup(tzd_ptr s, tzd_size_t max)
{
    tzd_size_t n = 0;
    tzd_ptr q;
    while (n < max && tzd_ptr_load8(tzd_ptr_add(s, (long) n)))
        n++;
    q = tzd_malloc(n + 1);
    if (q.base) {
        if (n)
            tzd_memcpy(q, s, n);
        tzd_ptr_store8(tzd_ptr_add(q, (long) n), 0);
    }
    return q;
}

