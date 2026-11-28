/* Bounds of pointers stored in memory (task #1, representation (b) strict).

   In Managed C a pointer stored in memory occupies 8 bytes, exactly as in C,
   so sizeof, struct layout and the host ABI are unchanged.  Its bounds are
   kept here, keyed by the address of the 8-byte slot.  cc1tzd emits

     tzd_sh_store(slot, v)    to write a pointer value into a slot,
     tzd_sh_load(slot)        when a pointer is read from a slot,
     tzd_sh_copy(dst, src, n) when memory holding slots is copied.

   A load checks that the slot still holds the address recorded by the last
   store.  If anything else changed those 8 bytes (a char/integer store, a
   host function, memset), the load traps: bounds are never guessed and can
   not be forged by writing bytes.  A slot holding 0 without an entry reads
   as the null pointer.

   Layout: a two-level table.  User addresses are below 2^47; slots are
   8-aligned.  The primary table has one entry per 4 MiB of address space;
   each secondary covers 4 MiB = 2^19 slots of 32 bytes.  Both are mapped
   MAP_NORESERVE and filled lazily, so only touched pages cost memory.  */
#include <d/pointer.h>
#include "sys.h"

#define SH_ADDR_BITS 47
#define SH_SEC_BITS  22                       /* bytes covered per secondary */
#define SH_PRI_N     (1UL << (SH_ADDR_BITS - SH_SEC_BITS))
#define SH_SEC_N     (1UL << (SH_SEC_BITS - 3))

typedef struct tzd_sh_ent {
    char *addr;        /* value recorded at the last store */
    char *base;
    tzd_size_t size;
    tzd_size_t live;     /* 1: entry valid (reserved for the #8 key later) */
} tzd_sh_ent;

/* An entry whose object ended while the slot still pointed at it (block
   exit of a local, see esc_release): live 0, base 0, this size, addr the
   value.  Loading the slot traps with its own message.  */
#define SH_DANGLING ((tzd_size_t) 0xdead10ca1UL)

static tzd_sh_ent **sh_pri;

void tzd_trap_slot(const char *msg, const void *slot, const void *value)
    __attribute__((noreturn));
static void sh_fail(const char *msg) __attribute__((noreturn));
static void sh_fail(const char *msg)
{
    tzd_trap(msg);
}

static tzd_size_t sh_index_check(const void *slot)
{
    tzd_size_t a = (tzd_size_t) slot;
    if (a & 7)
        sh_fail("libc2d: pointer slot is not 8-byte aligned\n");
    if (a >> SH_ADDR_BITS)
        sh_fail("libc2d: pointer slot outside the user address space\n");
    return a;
}

/* Which 4 KiB pages of the address space have had an entry written: one
   bit per page (4 GiB of address space for the map, MAP_NORESERVE; a page
   of it covers 128 MiB).  A page without the bit has no entries, so a
   lookup, a clear or a wipe there does not touch the table at all --
   before, every release wrote "not live" into 32 bytes of table per 8
   bytes released, which made a table page real for memory that never
   held a pointer (task #66: bash grew by 5 KiB per loop iteration).  */
static unsigned long *sh_used;
#define SH_USED_BYTES (1UL << (SH_ADDR_BITS - 12 - 3))

/* Which 8-byte slots have had an entry written: one bit per slot (2 TiB
   of address space for the map, MAP_NORESERVE; real only where pointers
   were stored, 1/64 of that memory).  A set bit can be stale (the entry
   is no longer live); a clear bit means no live entry.  With it a clear
   or a copy skips 512 bytes of pointer-free memory per word instead of
   reading 32 bytes of table per 8 bytes (free of a 2048-byte slot on a
   page that also holds pointers read 8 KiB of table).  */
static unsigned long *sh_live;
#define SH_LIVE_BYTES (1UL << (SH_ADDR_BITS - 3 - 3))

static int sh_page_used(tzd_size_t a)
{
    tzd_size_t pg = a >> 12;
    return sh_used && (sh_used[pg >> 6] >> (pg & 63)) & 1;
}

/* Entry for slot, or 0 when none can exist (no entry written on its page).  */
static tzd_sh_ent *sh_find(const void *slot)
{
    tzd_size_t a = sh_index_check(slot);
    tzd_sh_ent *sec;
    if (!sh_page_used(a))
        return 0;
    sec = sh_pri[a >> SH_SEC_BITS];
    return &sec[(a & ((1UL << SH_SEC_BITS) - 1)) >> 3];
}

/* Entry for slot, creating the tables as needed.  */
static tzd_sh_ent *sh_get(const void *slot)
{
    tzd_size_t a = sh_index_check(slot);
    tzd_sh_ent **pp;
    tzd_size_t pg = a >> 12;
    if (!sh_pri) {
        sh_pri = (tzd_sh_ent **) tzd_sys_mmap_noreserve(SH_PRI_N * sizeof *sh_pri);
        sh_used = (unsigned long *) tzd_sys_mmap_noreserve(SH_USED_BYTES);
        sh_live = (unsigned long *) tzd_sys_mmap_noreserve(SH_LIVE_BYTES);
        if (!sh_pri || !sh_used || !sh_live)
            sh_fail("libc2d: cannot map the pointer bounds table\n");
    }
    pp = &sh_pri[a >> SH_SEC_BITS];
    if (!*pp) {
        *pp = (tzd_sh_ent *) tzd_sys_mmap_noreserve(SH_SEC_N * sizeof(tzd_sh_ent));
        if (!*pp)
            sh_fail("libc2d: cannot map the pointer bounds table\n");
    }
    sh_used[pg >> 6] |= 1UL << (pg & 63);
    sh_live[a >> 9] |= 1UL << ((a >> 3) & 63);
    return &(*pp)[(a & ((1UL << SH_SEC_BITS) - 1)) >> 3];
}

/* Whether no slot overlapping [a, a+n) has its bit set.  */
static int sh_slots_unused(tzd_size_t a, tzd_size_t n)
{
    tzd_size_t i = a >> 3, last = (a + n - 1) >> 3;
    while (i <= last) {
        unsigned long w = sh_live[i >> 6];
        tzd_size_t lo = i & 63, hi = (last >> 6) == (i >> 6) ? (last & 63) : 63;
        unsigned long m = (hi == 63 ? ~0UL : (1UL << (hi + 1)) - 1) & ~((1UL << lo) - 1);
        if (w & m)
            return 0;
        i = (i | 63) + 1;
    }
    return 1;
}

/* Whether no slot of [a, a+n) can have a live entry: no page of it has
   had an entry written, or none of its slots has.  */
static int sh_range_unused(tzd_size_t a, tzd_size_t n)
{
    tzd_size_t pg = a >> 12, last = (a + n - 1) >> 12;
    if (!sh_used || (last >> (SH_ADDR_BITS - 12)))
        return !sh_used;
    for (; pg <= last; pg++) {
        if ((pg & 63) == 0 && last - pg >= 63) {
            if (sh_used[pg >> 6])
                return 0;
            pg += 63;
            continue;
        }
        if ((sh_used[pg >> 6] >> (pg & 63)) & 1)
            return sh_slots_unused(a, n);
    }
    return 1;
}

/* The address range [p, p+n), whole pages, is gone for good (a retired
   malloc run, a freed large block; its addresses are never used again):
   the table pages of its entries go back to the kernel.  */
void tzd_sh_release(void *p, tzd_size_t n)
{
    tzd_size_t a = (tzd_size_t) p, end = a + n;
    if (!sh_used)
        return;
    while (a < end) {
        tzd_size_t from, pg, i;
        tzd_sh_ent *sec;
        /* Skip 64 pages at a time where nothing was ever recorded.  */
        pg = a >> 12;
        if ((pg & 63) == 0 && end - a >= 64 * 4096UL && sh_used[pg >> 6] == 0) {
            a += 64 * 4096UL;
            continue;
        }
        if (!sh_page_used(a)) {
            a += 4096;
            continue;
        }
        /* A run of used pages inside one secondary: one madvise.  */
        from = a;
        while (a < end && sh_page_used(a)
               && (a >> SH_SEC_BITS) == (from >> SH_SEC_BITS)) {
            pg = a >> 12;
            sh_used[pg >> 6] &= ~(1UL << (pg & 63));
            for (i = 0; i < 8; i++)
                sh_live[(a >> 9) + i] = 0;
            a += 4096;
        }
        sec = sh_pri[from >> SH_SEC_BITS];
        tzd_sys_madvise(&sec[(from & ((1UL << SH_SEC_BITS) - 1)) >> 3],
                        (a - from) / 8 * sizeof(tzd_sh_ent), 4 /* MADV_DONTNEED */);
    }
}

tzd_ptr tzd_sh_load(const void *slot)
{
    char *v = *(char *const *) slot;
    tzd_sh_ent *e = sh_find(slot);
    tzd_ptr p;
    if (e && e->live && e->addr == v) {
        p.addr = e->addr;
        p.base = e->base;
        p.size = e->size;
        return p;
    }
    if (e && !e->live && e->size == SH_DANGLING && e->addr == v && v)
        tzd_trap_slot("tzd: pointer in memory points to a local whose block has "
                      "ended (CWE-562)\n", slot, v);
    if (v == 0) {
        /* A null slot of a heap block is a null pointer only when the
           program wrote it; fresh heap bytes are zero but hold no value
           (task #8, CWE-908).  */
        tzd_hp_read(slot, 8);
        return tzd_ptr_null();
    }
    if (e && e->live)
        tzd_trap_slot("tzd: pointer in memory was overwritten by a non-pointer store\n",
                      slot, v);
    tzd_trap_slot("tzd: pointer in memory has no bounds (not stored as a pointer)\n",
                  slot, v);
}

/* A union's pointer member named by `#pragma tzd lazy_pointer` (task #70):
   the union may hold another member's bytes there (bash reads
   REDIRECTEE.filename while it holds the int dest).  Then the value comes
   back with no bounds -- base = the value, size 0 -- so it can be tested
   and compared but any access through it, or through anything derived
   from it, traps.  A recorded pointer loads as usual.  */
tzd_ptr tzd_sh_load_lazy(const void *slot)
{
    char *v = *(char *const *) slot;
    tzd_sh_ent *e = sh_find(slot);
    tzd_ptr p;
    if (e && e->live && e->addr == v) {
        p.addr = e->addr;
        p.base = e->base;
        p.size = e->size;
        return p;
    }
    if (v == 0)
        return tzd_ptr_null();
    p.addr = v;
    p.base = v;
    p.size = 0;
    return p;
}

/* Pointers to stack objects kept in memory (task #8, CWE-562).

   A pointer to a local can be stored in a slot that outlives the local:
   `*out = buf` in a helper, a global, a heap struct.  The local's block
   ends, its bytes are wiped and its own slots dropped, but the slot that
   points at it still holds an entry with the local's bounds, and the next
   frame at that address would be read through it.  So every slot that
   receives a pointer whose object is not heap or static memory is noted
   here (slot -> object base), and a release that covers such an object
   (tzd_sh_wipe: block exit, and any other wipe) turns the entry of each
   noted slot still pointing at it into a "dangling" mark.  A load of that
   slot traps.  The table holds exactly the live entries of that kind: a
   store of another value or a clear of the slot takes it out, so its size
   is bounded by the slots that hold such pointers right now.  Stack
   addresses are reused, unlike heap addresses, which is why the heap map
   cannot be used for this.  Not covered: a pointer to a local kept only
   in a register or a local variable whose address is never taken.  */
#define ESC_EMPTY 0UL
#define ESC_GONE  1UL                   /* never a slot: slots are 8-aligned */
typedef struct { unsigned long slot, base, size; } sh_esc;
static sh_esc *esc_tab;
static tzd_size_t esc_cap, esc_n, esc_gone;
static unsigned long esc_lo = ~0UL, esc_hi;   /* covers every noted object */
static volatile int esc_lock;
extern char __executable_start[] __attribute__((weak));
extern char _end[] __attribute__((weak));

static void esc_enter(void)
{
    while (__atomic_exchange_n(&esc_lock, 1, __ATOMIC_ACQUIRE))
        __builtin_ia32_pause();
}
static void esc_leave(void)
{
    __atomic_store_n(&esc_lock, 0, __ATOMIC_RELEASE);
}

/* An object that a release can end while a pointer to it is kept: not the
   heap (its addresses are never reused; the heap map covers it), not the
   program image (static objects never end), not host memory.  */
static int esc_kind(tzd_ptr v)
{
    unsigned long b = (unsigned long) v.base;
    if (!b || v.size == (tzd_size_t) (~(tzd_size_t) 0 >> 1))
        return 0;
    if (b - tzd_hp_lo < tzd_hp_span)
        return 0;
    if (__executable_start && _end
        && b >= (unsigned long) __executable_start && b < (unsigned long) _end)
        return 0;
    return 1;
}

static tzd_size_t esc_hash(unsigned long slot, tzd_size_t cap)
{
    return (tzd_size_t) ((slot >> 3) * 0x9E3779B97F4A7C15UL) & (cap - 1);
}

static void esc_put_locked(unsigned long slot, unsigned long base,
                           tzd_size_t size);

static void esc_grow(void)
{
    sh_esc *old = esc_tab;
    tzd_size_t oldcap = esc_cap, i, cap = esc_cap ? esc_cap : 256;
    while ((esc_n + 1) * 2 > cap)
        cap *= 2;
    if (cap == esc_cap && esc_gone == 0)
        cap *= 2;
    esc_tab = (sh_esc *) tzd_sys_mmap(cap * sizeof *esc_tab);
    if (!esc_tab)
        sh_fail("libc2d: out of memory for the table of kept stack pointers\n");
    esc_cap = cap;
    esc_n = esc_gone = 0;
    for (i = 0; i < oldcap; i++)
        if (old[i].slot > ESC_GONE)
            esc_put_locked(old[i].slot, old[i].base, old[i].size);
    if (old)
        tzd_sys_munmap(old, oldcap * sizeof *old);
}

static void esc_put_locked(unsigned long slot, unsigned long base,
                           tzd_size_t size)
{
    tzd_size_t i, gone = (tzd_size_t) -1;
    if ((esc_n + esc_gone + 1) * 2 > esc_cap)
        esc_grow();
    for (i = esc_hash(slot, esc_cap); esc_tab[i].slot != ESC_EMPTY;
         i = (i + 1) & (esc_cap - 1)) {
        if (esc_tab[i].slot == slot) {
            esc_tab[i].base = base;
            esc_tab[i].size = size;
            goto bounds;
        }
        if (esc_tab[i].slot == ESC_GONE && gone == (tzd_size_t) -1)
            gone = i;
    }
    if (gone != (tzd_size_t) -1) {
        i = gone;
        esc_gone--;
    }
    esc_tab[i].slot = slot;
    esc_tab[i].base = base;
    esc_tab[i].size = size;
    esc_n++;
bounds:
    if (base < esc_lo)
        esc_lo = base;
    if (base + size > esc_hi)
        esc_hi = base + size;
}

static void esc_del_locked(unsigned long slot)
{
    tzd_size_t i;
    for (i = esc_hash(slot, esc_cap); esc_tab[i].slot != ESC_EMPTY;
         i = (i + 1) & (esc_cap - 1))
        if (esc_tab[i].slot == slot) {
            esc_tab[i].slot = ESC_GONE;
            esc_n--;
            esc_gone++;
            return;
        }
}

/* SLOT now holds V (or nothing): note it, or take it out.  */
static void esc_note(const void *slot, tzd_ptr v)
{
    int kind = v.addr || v.base ? esc_kind(v) : 0;
    if (!kind && !esc_n)
        return;
    esc_enter();
    if (kind)
        esc_put_locked((unsigned long) slot, (unsigned long) v.base, v.size);
    else
        esc_del_locked((unsigned long) slot);
    esc_leave();
}

/* [p, p+n) is released: every noted slot whose object lies in it gets the
   dangling mark (if its entry still points there), and leaves the table.  */
static void esc_release(tzd_size_t p, tzd_size_t n)
{
    tzd_size_t i;
    unsigned long lo = ~0UL, hi = 0;
    if (!esc_n || p >= esc_hi || p + n <= esc_lo)
        return;
    esc_enter();
    for (i = 0; i < esc_cap; i++) {
        sh_esc *x = &esc_tab[i];
        tzd_sh_ent *e;
        if (x->slot <= ESC_GONE)
            continue;
        if (x->base < p || x->base >= p + n) {
            if (x->base < lo)
                lo = x->base;
            if (x->base + x->size > hi)
                hi = x->base + x->size;
            continue;
        }
        e = sh_find((void *) x->slot);
        if (e && e->live == 1 && (unsigned long) e->base == x->base) {
            e->live = 0;
            e->base = 0;
            e->size = SH_DANGLING;      /* e->addr keeps the value */
        }
        x->slot = ESC_GONE;
        esc_n--;
        esc_gone++;
    }
    esc_lo = lo;
    esc_hi = hi;
    esc_leave();
}

/* A copy gave SLOT the entry SE (tzd_sh_copy).  */
static void sh_copied(void *slot, const tzd_sh_ent *se)
{
    tzd_ptr v;
    v.addr = se->addr;
    v.base = se->base;
    v.size = se->size;
    if (se->live == 1 || esc_n)
        esc_note(slot, se->live == 1 ? v : tzd_ptr_null());
}

/* Record v as the bounds of slot, which already holds v.addr.  */
static void sh_record(const void *slot, tzd_ptr v)
{
    tzd_sh_ent *e;
    esc_note(slot, v);
    if (v.addr == 0 && v.base == 0) {
        e = sh_find(slot);
        if (e)
            e->live = 0;
        return;
    }
    e = sh_get(slot);
    e->addr = v.addr;
    e->base = v.base;
    e->size = v.size;
    e->live = 1;
}

/* Pointer store: write v.addr into the 8-byte slot and record its bounds.
   Value and entry change together, so the compiler cannot write one
   without the other.  */
void tzd_sh_store(void *slot, tzd_ptr v)
{
    sh_index_check(slot);
    *(char **) slot = v.addr;
    sh_record(slot, v);
    tzd_hp_wrote(slot, 8);
}

/* The slot already holds the value (static data, possibly read-only after
   relocation; slots a host function wrote).  */
void tzd_sh_record(const void *slot, tzd_ptr v)
{
    if ((tzd_size_t) slot & 7)
        return;      /* packed host field: no entry, its loads use the rule */
    sh_record(slot, v);
    tzd_hp_wrote(slot, 8);
}

/* Forget every slot entirely inside [p, p+n) and every slot overlapping it:
   after those bytes change, the old bounds must not be reachable again.  */
void tzd_sh_clear(void *p, tzd_size_t n)
{
    tzd_size_t a, end;
    if (!sh_used || n == 0)
        return;
    a = (tzd_size_t) p & ~7UL;
    end = (tzd_size_t) p + n;
    if (a >> SH_ADDR_BITS || (end - 1) >> SH_ADDR_BITS)
        return;
    while (a < end) {
        tzd_size_t stop = (a & ~4095UL) + 4096;
        tzd_sh_ent *e;
        if (stop > end)
            stop = end;
        /* One look per page; a page that never had an entry is skipped
           without touching the table.  */
        if (!sh_page_used(a)) {
            a = stop;
            continue;
        }
        e = &sh_pri[a >> SH_SEC_BITS][(a & ((1UL << SH_SEC_BITS) - 1)) >> 3];
        /* Within the page: by the slot bits, a word (64 slots, 512
           bytes) at a time.  */
        while (a < stop) {
            tzd_size_t i = a >> 3, lo = i & 63;
            tzd_size_t lasts = (stop - 1) >> 3;
            tzd_size_t hi = (lasts >> 6) == (i >> 6) ? (lasts & 63) : 63;
            unsigned long m = (hi == 63 ? ~0UL : (1UL << (hi + 1)) - 1)
                              & ~((1UL << lo) - 1);
            unsigned long w = sh_live[i >> 6] & m;
            sh_live[i >> 6] &= ~m;
            while (w) {
                unsigned b = (unsigned) __builtin_ctzl(w);
                w &= w - 1;
                e[b - lo].live = 0;
                if (esc_n)
                    esc_note((void *) ((i - lo + b) << 3), tzd_ptr_null());
            }
            e += hi - lo + 1;
            a = ((i | 63) + 1) << 3;
            if (hi != 63)
                break;
        }
        a = stop;
    }
}

/* Destroy memory that is being released (scope exit, free): every byte is
   overwritten once with a fixed meaningless value, 0xa5 (TK 2026-11-17:
   the two stages used until then -- zero, then other bytes -- added
   nothing; as a word the value is not zero, not a small integer and not
   a canonical address), and the bounds entries are removed.  A stale
   pointer value cannot survive in the bytes.  Whole words where the
   range allows.  kc2d and cc1tzd's inline wipes use the same value.  */
#define SH_WIPE_BYTE 0xa5
#define SH_WIPE_WORD 0xa5a5a5a5a5a5a5a5UL

static void sh_wipe_bytes(void *p, tzd_size_t n);

void tzd_sh_wipe_data(void *p, tzd_size_t n)
{
    /* Memory that holds no pointer can still be pointed at: the slots
       kept pointing in here get the dangling mark (task #8).  */
    esc_release((tzd_size_t) p, n);
    sh_wipe_bytes(p, n);
}

/* __tzd_wipe in user space (an allocator recycling memory it keeps, task
   #3): the bytes are destroyed and the bounds entries inside dropped, but
   the storage does not end, so pointers kept to it are not marked.  */
void tzd_wipe_range(void *p, tzd_size_t n)
{
    sh_wipe_bytes(p, n);
    tzd_sh_clear(p, n);
}

static void sh_wipe_bytes(void *p, tzd_size_t n)
{
    unsigned char *b = (unsigned char *) p;
    tzd_size_t i = 0;
    for (; i < n && (((tzd_size_t) b + i) & 7) != 0; i++)
        ((volatile unsigned char *) b)[i] = SH_WIPE_BYTE;
    for (; i + 8 <= n; i += 8)
        *(volatile tzd_size_t *) (b + i) = SH_WIPE_WORD;
    for (; i < n; i++)
        ((volatile unsigned char *) b)[i] = SH_WIPE_BYTE;
    __asm__ volatile ("" : : "r"(b) : "memory");
}

void tzd_sh_wipe(void *p, tzd_size_t n)
{
    tzd_sh_wipe_data(p, n);             /* also marks slots pointing here */
    tzd_sh_clear(p, n);
}

/* Bytes [src, src+n) were (or are about to be) copied to [dst, dst+n).
   Slots move with them when both sides keep the same 8-byte phase; any
   destination slot that does not receive a whole source slot is cleared.
   Overlap is handled like memmove.  */
void tzd_sh_copy(void *dst, const void *src, tzd_size_t n)
{
    tzd_size_t d = (tzd_size_t) dst, s = (tzd_size_t) src, i;
    /* Which heap granules hold written bytes moves with the bytes too
       (task #8), whether or not any slot is involved.  */
    tzd_hp_copy(dst, src, n);
    if (!sh_pri || n == 0 || d == s)
        return;
    /* Neither side has an entry on any of its pages (the usual copy of
       bytes): nothing to move, nothing to clear.  */
    if (sh_range_unused(s, n) && sh_range_unused(d, n))
        return;
    if (((d ^ s) & 7) != 0) {
        tzd_sh_clear(dst, n);
        return;
    }
    {
        /* First whole slot inside the range.  */
        tzd_size_t lead = (8 - (d & 7)) & 7;
        tzd_size_t cnt = n > lead ? (n - lead) / 8 : 0;
        /* Partial slots at both edges change bytes of a slot: clear.  */
        if (lead)
            tzd_sh_clear(dst, lead < n ? lead : n);
        if (n > lead + cnt * 8)
            tzd_sh_clear((char *) dst + lead + cnt * 8, n - lead - cnt * 8);
        d += lead;
        s += lead;
        if (d < s) {
            for (i = 0; i < cnt; i++) {
                tzd_sh_ent *se = sh_find((void *) (s + i * 8));
                if (se && se->live) {
                    *sh_get((void *) (d + i * 8)) = *se;
                    sh_copied((void *) (d + i * 8), se);
                }
                else
                    tzd_sh_clear((void *) (d + i * 8), 8);
            }
        } else {
            for (i = cnt; i > 0; i--) {
                tzd_sh_ent *se = sh_find((void *) (s + (i - 1) * 8));
                if (se && se->live) {
                    *sh_get((void *) (d + (i - 1) * 8)) = *se;
                    sh_copied((void *) (d + (i - 1) * 8), se);
                }
                else
                    tzd_sh_clear((void *) (d + (i - 1) * 8), 8);
            }
        }
    }
}

/* __tzd_copy (cc1tzd; memcpy and memmove): N bytes from SRC to DST, both
   ranges already checked by the caller (bounds, and that the heap blocks
   are live).  The bytes move like memmove, the slots' bounds entries and
   the heap map's written bits move with them.  Copying is not reading:
   bytes that were never written stay "never written" at the destination
   instead of trapping here (a struct with padding, the unused tail of a
   buffer).  Kept a plain loop: GCC must not turn it into a memmove call
   (there is no other memmove under libc2d).  */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void tzd_copy_bytes(void *dst, const void *src, tzd_size_t n)
{
    unsigned char *d = (unsigned char *) dst;
    const unsigned char *s = (const unsigned char *) src;
    tzd_size_t i;
    if (d <= s || d >= s + n)
        for (i = 0; i < n; i++)
            d[i] = s[i];
    else
        for (i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    tzd_sh_copy(dst, src, n);
}

/* Static initializers: every unit's `tzd_shinit` table lists
   {slot, base, size} for the pointers in its initialized data.  */
struct tzd_sh_init { char *slot; char *base; tzd_size_t size; };
static void sh_init_typed(void);
extern const struct tzd_sh_init __start_tzd_shinit[] __attribute__((weak));
extern const struct tzd_sh_init __stop_tzd_shinit[] __attribute__((weak));

void tzd_sh_init_static(void)
{
    const struct tzd_sh_init *t = __start_tzd_shinit;
    sh_init_typed();
    if (!t)
        return;
    for (; t < __stop_tzd_shinit; t++) {
        tzd_ptr v;
        if (t->slot == 0)
            continue;
        v.addr = *(char **) t->slot;
        v.base = t->base;
        v.size = t->size;
        /* Top bit: the size is read from a __tzd_size.NAME symbol (an
           extern array whose size is known at link time).  */
        if (v.size >> 63)
            v.size = *(const tzd_size_t *) (v.size - ((tzd_size_t) 1 << 63));
        tzd_sh_record(t->slot, v);
    }
}

/* #pragma tzd fnptr_holds (task #70): a function-pointer member that may
   also hold data pointers of the listed types (readline's KEYMAP_ENTRY
   .function: a function, a child keymap or a macro string).  A data
   pointer stored there is recorded with its bounds and a type id in
   `live` (an id is never 0 or 1); a function stored there drops the entry.
   Reading it back as a data pointer of type id ID needs a live entry for
   the same value and the same id (ID 0: a char or void view, any type);
   otherwise it traps (a function, or the wrong kind of object).  */
void tzd_sh_record_typed(void *slot, tzd_ptr v, tzd_size_t id)
{
    tzd_sh_ent *e;
    sh_index_check(slot);
    tzd_hp_wrote(slot, 8);          /* the caller stored the 8 bytes */
    if (v.addr == 0) {
        e = sh_find(slot);
        if (e)
            e->live = 0;
        return;
    }
    e = sh_get(slot);
    e->addr = v.addr;
    e->base = v.base;
    e->size = v.size;
    e->live = id;
}

tzd_ptr tzd_sh_load_typed(const void *slot, tzd_size_t id)
{
    char *v = *(char *const *) slot;
    tzd_sh_ent *e = sh_find(slot);
    tzd_ptr p;
    if (v == 0)
        return tzd_ptr_null();
    if (e && e->live > 1 && e->addr == v && (id == 0 || e->live == id)) {
        p.addr = e->addr;
        p.base = e->base;
        p.size = e->size;
        return p;
    }
    if (e && e->live > 1 && e->addr == v)
        tzd_trap_slot("tzd: data pointer read back as a different type "
                      "(#pragma tzd fnptr_holds)\n", slot, v);
    tzd_trap_slot("tzd: function pointer member read as a data pointer but it "
                  "holds no recorded data pointer (#pragma tzd fnptr_holds)\n",
                  slot, v);
}

/* Static initializers of such members: {slot, base, size, id} in section
   tzd_shtyped.  */
struct tzd_sh_typed_init { char *slot; char *base; tzd_size_t size, id; };
extern const struct tzd_sh_typed_init __start_tzd_shtyped[] __attribute__((weak));
extern const struct tzd_sh_typed_init __stop_tzd_shtyped[] __attribute__((weak));

static void sh_init_typed(void)
{
    const struct tzd_sh_typed_init *t = __start_tzd_shtyped;
    if (!t)
        return;
    for (; t < __stop_tzd_shtyped; t++) {
        tzd_ptr v;
        if (t->slot == 0)
            continue;
        v.addr = *(char **) t->slot;
        v.base = t->base;
        v.size = t->size;
        tzd_sh_record_typed(t->slot, v, t->id);
    }
}

/* Recorded bounds of slot regardless of its current value (boundary.c
   uses it to recover the bounds of pointers a host function moved).
   Returns 0 when the slot has no entry.  */
int tzd_sh_peek(const void *slot, tzd_ptr *out)
{
    tzd_sh_ent *e = sh_find(slot);
    if (!e || !e->live)
        return 0;
    out->addr = e->addr;
    out->base = e->base;
    out->size = e->size;
    return 1;
}

/* Does slot hold exactly the value its live entry was recorded for?
   (cc1tzd: raw pointer fields of host structs that user code wrote.)  */
int tzd_sh_match(const void *slot)
{
    tzd_sh_ent *e;
    if ((tzd_size_t) slot & 7)
        return 0;                      /* packed host field: never recorded */
    e = sh_find(slot);
    return e && e->live && e->addr == *(char *const *) slot;
}
