/* kc2d: the kernel-side TzD runtime (task #42).  Linked into vmlinux;
   compiled by plain GCC with the kernel's flags (tests/kernel/build_tzd.sh).

   The same entry points as libc2d's core (shadow.c, pointer.c, regs.c):
     tzd_sh_load/store/record/copy/clear/wipe/match   pointer slots in memory
     tzd_trap                                         stop: panic (TK)
     tzd_vregs_clear                                  no-op (kernel: no SSE)
     tzd_from_native                                  bounds for a raw address
   plus the checked mem and string functions cc1tzd redirects to.

   Bounds table ("影", TK 2026-11-13): a shadow structure shaped like a
   4-level page table over the 48-bit address: 512-entry tables for bits
   47..39, 38..30 and 29..21, then a leaf block per 4 KiB page holding one
   32-byte entry per 8-byte slot (16 KiB).  Tables and leaves come from a
   static pool, not from the page allocator (which is itself compiled by
   TzD and would recurse); an empty pool panics.  Updates run with
   interrupts off (no SMP yet: task #49).

   Calling TzD code from here: a TzD function takes each pointer argument
   as a 24-byte {addr, base, size} record (tzd_ptr below), so panic is
   declared with that type.

   Early boot: __startup_64 runs from the identity mapping, where this
   file's absolute addresses (the pool, the tables) are not mapped yet.
   While running there, the table is not used: loads give the whole low
   address space as bounds, stores only write the slot (要確認, task #42).

   A slot whose value has no matching entry (written as an integer) loads
   like an integer turned into a pointer: tzd_from_native's region bounds
   (要確認, task #42).  */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/irqflags.h>
#include <linux/mm.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>
#include <asm/cpu_entry_area.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/current.h>
#include <asm/page_64_types.h>
#include <asm/io.h>
/* struct slab / kmem_cache, for the pool statistics (sh_stat_leaf).  */
#include <linux/../../mm/slab.h>

/* The entry points are called by cc1tzd's generated code, which declares
   them itself: no header.  */
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

typedef struct tzd_ptr {
	char *addr;
	char *base;
	unsigned long size;
} tzd_ptr;

/* ---------------------------------------------------------------- trap */

extern void tzd_panic(tzd_ptr fmt, ...) __asm__("panic") __attribute__((noreturn));

static int kc2d_in_trap __section(".data");	/* .bss: see sh_bss_mark */

static bool kc2d_identity(void)
{
	unsigned long rip;

	asm volatile("lea 0(%%rip), %0" : "=r"(rip));
	return rip < __START_KERNEL_map;
}

static __always_inline tzd_ptr kc2d_obj(const char *s, unsigned long n)
{
	tzd_ptr p = { (char *)s, (char *)s, n };
	return p;
}

/* The trap message straight to COM1, before anything else: the console
   may not be up yet (early boot), and panic may trap again.  */
static void kc2d_serial_putc(char c)
{
	unsigned int i;

	for (i = 0; i < 100000; i++)
		if (inb(0x3f8 + 5) & 0x20)	/* transmitter empty */
			break;
	outb(c, 0x3f8);
}

static void kc2d_serial_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			kc2d_serial_putc('\r');
		kc2d_serial_putc(*s++);
	}
}

static void kc2d_serial_hex(unsigned long v)
{
	int i;

	kc2d_serial_puts("0x");
	for (i = 60; i >= 0; i -= 4)
		kc2d_serial_putc("0123456789abcdef"[(v >> i) & 15]);
}

static void kc2d_serial_dec(unsigned long v)
{
	char b[21];
	int i = 20;

	b[i] = 0;
	do {
		b[--i] = '0' + v % 10;
		v /= 10;
	} while (v);
	kc2d_serial_puts(b + i);
}

void __noreturn tzd_trap(const char *msg)
{
	static const char fmt[] = "%s";
	unsigned long n = 0;

	kc2d_serial_puts("\n[kc2d] ");
	kc2d_serial_puts(msg);
	kc2d_serial_puts("[kc2d] called from ");
	kc2d_serial_hex((unsigned long)__builtin_return_address(0));
	kc2d_serial_puts("\n");
	{
		/* The callers above, from the frame pointer chain.  */
		unsigned long *fp = __builtin_frame_address(0);
		int k;

		for (k = 0; k < 10 && fp; k++) {
			unsigned long *up = (unsigned long *)fp[0];

			if (up <= fp || (char *)up - (char *)fp > 65536)
				break;
			kc2d_serial_puts("[kc2d]   ");
			kc2d_serial_hex(up[1]);
			kc2d_serial_puts("\n");
			fp = up;
		}
	}

	if (kc2d_identity() || kc2d_in_trap) {
		for (;;)
			asm volatile("cli; ud2; hlt");
	}
	kc2d_in_trap = 1;
	while (msg[n])
		n++;
	tzd_panic(kc2d_obj(fmt, sizeof fmt), kc2d_obj(msg, n + 1));
}

/* An out-of-bounds access found by cc1tzd's check: the access and the
   object's bounds on COM1, then the trap.  */
void __noreturn tzd_trap_bounds(const char *msg, const void *addr,
				const void *base, unsigned long size,
				unsigned long n)
{
	kc2d_serial_puts("\n[kc2d] access ");
	kc2d_serial_hex((unsigned long)addr);
	kc2d_serial_puts(" n ");
	kc2d_serial_hex(n);
	kc2d_serial_puts(" object ");
	kc2d_serial_hex((unsigned long)base);
	kc2d_serial_puts(" size ");
	kc2d_serial_hex(size);
	tzd_trap(msg);
}

static void __noreturn kc2d_fail(const char *msg)
{
	tzd_trap(msg);
}

/* Kernel code is built without SSE: no vector register holds a pointer.  */
void tzd_vregs_clear(void)
{
}

/* --------------------------------------------------------------- table */

typedef struct tzd_sh_ent {
	char *addr;		/* value recorded at the last store */
	char *base;
	unsigned long size;
	unsigned long live;
} tzd_sh_ent;

#define SH_LEAF_N	(PAGE_SIZE / 8)			/* slots per page */
#define SH_LEAF_BYTES	(SH_LEAF_N * sizeof(tzd_sh_ent))	/* 16 KiB */
#define SH_POOL_BYTES	(512UL << 20)	/* TK 2026-11-16 (was 192) */

/* The kernel clears .bss only in x86_64_start_kernel (clear_bss), after
   TzD code has already run on the kernel mapping: until then .bss holds
   whatever the decompressor left.  So the table's root and counters are
   in .data, the pool (.bss) is zeroed here as it is handed out, and a
   mark written into .bss tells when clear_bss has wiped the pool: the
   table then starts over (records made before it are gone; such slots
   load as unrecorded values, tzd_sh_load).  */
extern void *__memset(void *, int, size_t);
#define SH_BSS_MARK	0x747a64626f6f7431UL	/* "tzdboot1" */
static char sh_pool[SH_POOL_BYTES] __aligned(PAGE_SIZE);
static unsigned long sh_bss_mark;
static unsigned long sh_pool_used __section(".data");
static void **sh_top[512] __section(".data");
static int sh_inited __section(".data");
enum { ST_DIRECT, ST_VMALLOC, ST_VMEMMAP, ST_IMAGE, ST_OTHER, ST_N };
static unsigned long sh_stat[ST_N] __section(".data");
static unsigned long sh_stat_next __section(".data");
static unsigned long sh_vm_lo __section(".data"), sh_vm_hi __section(".data");
static unsigned long sh_proc_boot __section(".data");	/* sh_stat_proc */
void tzd_sh_init_static(void);

/* The static initializers are registered before the table is first used
   (after the switch to the kernel mapping), and again after clear_bss.  */
/* The leaf of the last pages looked up (task #69: the four-level walk was
   a fifth of the kernel's time).  One word per cache line, so a reader
   needs no lock against an interrupt: it takes the leaf pointer and
   checks, in sh_leaf_pg, that this leaf is the one of the page it wants.
   sh_leaf_pg[pool page of a leaf] = the page it describes, written once
   when the leaf is made and never changed (leaves are never freed).  */
#define SH_LC_N		1024
static tzd_sh_ent *sh_lc[SH_LC_N] __section(".data");
static unsigned long sh_leaf_pg[SH_POOL_BYTES >> PAGE_SHIFT];
#define SH_LC_H(pg)	(((pg) ^ ((pg) >> 10)) & (SH_LC_N - 1))

static __always_inline void sh_ready(void)
{
	if (unlikely(sh_inited && sh_bss_mark != SH_BSS_MARK)) {
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(sh_top); i++)
			sh_top[i] = NULL;
		for (i = 0; i < SH_LC_N; i++)
			sh_lc[i] = NULL;
		sh_pool_used = 0;
		sh_inited = 0;
		for (i = 0; i < ARRAY_SIZE(sh_stat); i++)
			sh_stat[i] = 0;
		sh_stat_next = 0;
		sh_vm_lo = sh_vm_hi = 0;
		sh_proc_boot = 0;
	}
	if (unlikely(!sh_inited)) {
		sh_inited = 1;
		sh_bss_mark = SH_BSS_MARK;
		tzd_sh_init_static();
	}
}

static void sh_stat_proc_dump(int top);

static void *sh_alloc(unsigned long n)
{
	void *p;

	if (sh_pool_used + n > SH_POOL_BYTES) {
		static int dumped __section(".data");

		if (!dumped) {	/* once: the dump itself must not re-enter */
			dumped = 1;
			kc2d_serial_puts("\n[kc2d] table full; leaves so far:\n");
			sh_stat_proc_dump(0);
		}
		kc2d_fail("tzd: the kernel pointer bounds table is full\n");
	}
	p = sh_pool + sh_pool_used;
	sh_pool_used += n;
	__memset(p, 0, n);	/* .bss may not be cleared yet */
	return p;
}

static __always_inline unsigned long sh_index_check(const void *slot)
{
	unsigned long a = (unsigned long)slot;

	if (a & 7)
		kc2d_fail("tzd: pointer slot is not 8-byte aligned\n");
	/* 48-bit canonical addresses (4-level paging).  */
	if ((long)a >> 47 != 0 && (long)a >> 47 != -1)
		kc2d_fail("tzd: pointer slot outside the 48-bit address space\n");
	return a;
}

#define SH_I1(a)	(((a) >> 39) & 511)
#define SH_I2(a)	(((a) >> 30) & 511)
#define SH_I3(a)	(((a) >> 21) & 511)
#define SH_I4(a)	(((a) >> 12) & 511)
#define SH_IS(a)	(((a) >> 3) & (SH_LEAF_N - 1))

/* Entry for slot, or NULL when none exists.  */
static __always_inline tzd_sh_ent *sh_cached(unsigned long a)
{
	unsigned long pg = a >> PAGE_SHIFT;
	tzd_sh_ent *l = READ_ONCE(sh_lc[SH_LC_H(pg)]);

	if (l && sh_leaf_pg[((char *)l - sh_pool) >> PAGE_SHIFT] == pg)
		return l;
	return NULL;
}

static tzd_sh_ent *sh_find(const void *slot)
{
	unsigned long a = sh_index_check(slot);
	void **t2, **t3, **t4;
	tzd_sh_ent *leaf;

	sh_ready();

	leaf = sh_cached(a);
	if (leaf)
		return &leaf[SH_IS(a)];
	t2 = sh_top[SH_I1(a)];
	if (!t2)
		return NULL;
	t3 = t2[SH_I2(a)];
	if (!t3)
		return NULL;
	t4 = t3[SH_I3(a)];
	if (!t4)
		return NULL;
	leaf = t4[SH_I4(a)];
	if (!leaf)
		return NULL;
	WRITE_ONCE(sh_lc[SH_LC_H(a >> PAGE_SHIFT)], leaf);
	return &leaf[SH_IS(a)];
}

/* Pool use by kernel address range (task #59 investigation: what fills
   the table).  Leaves are counted per range; every 8 MiB of pool growth
   one line on the serial console, plus the lowest/highest vmalloc leaf
   (vmalloc'd kernel stacks marching through the range would show as a
   rising maximum).  */
/* Direct-map leaves by page kind, and by slab cache.  */
#define SH_NC 24
static unsigned long sh_pg_slab __section(".data"), sh_pg_table __section(".data"),
	sh_pg_other __section(".data");
static struct kmem_cache *sh_cache[SH_NC] __section(".data");
/* The name is copied when the cache is first seen: a cache can be
   destroyed later (its kmem_cache freed and wiped), and the pointer kept
   here only identifies it (a new cache at the same address counts on).  */
static char sh_cache_nm[SH_NC][24] __section(".data");
static unsigned long sh_cache_n[SH_NC] __section(".data");

static void sh_stat_page(unsigned long a)
{
	struct page *pg;
	struct folio *f;
	struct kmem_cache *c;
	int i;

	if (!sh_bss_mark || !virt_addr_valid((void *)a)) {
		sh_pg_other++;
		return;
	}
	pg = virt_to_page((void *)a);
	f = page_folio(pg);
	if (folio_test_slab(f)) {
		sh_pg_slab++;
		c = ((struct slab *)f)->slab_cache;	/* folio_slab() */
		for (i = 0; i < SH_NC; i++) {
			if (sh_cache[i] == c || !sh_cache[i]) {
				if (!sh_cache[i]) {
					const char *n = c->name ? c->name : "?";
					int j;

					for (j = 0; j < 23 && n[j]; j++)
						sh_cache_nm[i][j] = n[j];
					sh_cache_nm[i][j] = 0;
				}
				sh_cache[i] = c;
				sh_cache_n[i]++;
				return;
			}
		}
	} else if (PageTable(pg)) {
		sh_pg_table++;
	} else {
		sh_pg_other++;
	}
}

/* Leaves by the process that created them (TK 2026-11-16: the breakdown
   per pid).  A leaf is charged to the task whose pointer store first
   touched that 4 KiB page, keyed by (tgid, comm) so a fork and its exec
   show apart; later users of the same page (a slab page reused by another
   process) are not charged.  Before the scheduler is up: "boot"; outside
   task context (irq, softirq, NMI): "irq".  */
#define SH_NP 128
struct sh_proc {
	int tgid;
	char comm[TASK_COMM_LEN];
	unsigned long n;
};
static struct sh_proc sh_proc[SH_NP] __section(".data");
static unsigned long sh_proc_irq __section(".data"), sh_proc_rest __section(".data");

static void sh_stat_proc(void)
{
	struct task_struct *t;
	int i, j;

	if (system_state < SYSTEM_SCHEDULING) {
		sh_proc_boot++;
		return;
	}
	if (!in_task()) {
		sh_proc_irq++;
		return;
	}
	t = current;
	/* No strncmp: the kernel's is TzD code (24-byte pointer arguments).  */
	for (i = 0; i < SH_NP && sh_proc[i].n; i++) {
		if (sh_proc[i].tgid != t->tgid)
			continue;
		for (j = 0; j < TASK_COMM_LEN - 1; j++)
			if (sh_proc[i].comm[j] != t->comm[j] || !t->comm[j])
				break;
		if (j == TASK_COMM_LEN - 1 || sh_proc[i].comm[j] == t->comm[j]) {
			sh_proc[i].n++;
			return;
		}
	}
	if (i == SH_NP) {
		sh_proc_rest++;
		return;
	}
	sh_proc[i].tgid = t->tgid;
	for (j = 0; j < TASK_COMM_LEN - 1 && t->comm[j]; j++)
		sh_proc[i].comm[j] = t->comm[j];
	sh_proc[i].comm[j] = 0;
	sh_proc[i].n = 1;
}

/* The top processes by leaves (all of them when top is 0), in KiB of
   table (16 KiB a leaf).  */
static void sh_stat_proc_dump(int top)
{
	static unsigned char done[SH_NP] __section(".data");
	unsigned long sum = 0;
	int i, k, m, nn;

	for (nn = 0; nn < SH_NP && sh_proc[nn].n; nn++) {
		done[nn] = 0;
		sum += sh_proc[nn].n;
	}
	kc2d_serial_puts("[kc2d] by process (leaves, KiB): boot ");
	kc2d_serial_dec(sh_proc_boot);
	kc2d_serial_puts(" irq ");
	kc2d_serial_dec(sh_proc_irq);
	kc2d_serial_puts(" tasks ");
	kc2d_serial_dec(sum);
	kc2d_serial_puts(" untracked ");
	kc2d_serial_dec(sh_proc_rest);
	kc2d_serial_puts("\n");
	for (k = 0; k < nn && (!top || k < top); k++) {
		m = -1;
		for (i = 0; i < nn; i++)
			if (!done[i] && (m < 0 || sh_proc[i].n > sh_proc[m].n))
				m = i;
		done[m] = 1;
		kc2d_serial_puts("[kc2d]   pid ");
		kc2d_serial_dec(sh_proc[m].tgid);
		kc2d_serial_puts(" ");
		kc2d_serial_puts(sh_proc[m].comm);
		kc2d_serial_puts(" ");
		kc2d_serial_dec(sh_proc[m].n);
		kc2d_serial_puts(" ");
		kc2d_serial_dec(sh_proc[m].n * (SH_LEAF_BYTES >> 10));
		kc2d_serial_puts("\n");
	}
}

static void sh_stat_leaf(unsigned long a)
{
	static const char *const nm[ST_N] = {
		" direct ", " vmalloc ", " vmemmap ", " image ", " other " };
	int k;

	sh_stat_proc();
	if (a >= __START_KERNEL_map)
		k = ST_IMAGE;
	else if (a >= VMEMMAP_START && a < VMEMMAP_START + (1UL << 40))
		k = ST_VMEMMAP;
	else if (a >= VMALLOC_START && a <= VMALLOC_END)
		k = ST_VMALLOC;
	else if (a >= PAGE_OFFSET && a < PAGE_OFFSET + (1UL << 46))
		k = ST_DIRECT;
	else
		k = ST_OTHER;
	sh_stat[k]++;
	if (k == ST_DIRECT)
		sh_stat_page(a);
	if (k == ST_VMALLOC) {
		if (!sh_vm_lo || a < sh_vm_lo)
			sh_vm_lo = a;
		if (a > sh_vm_hi)
			sh_vm_hi = a;
	}
	if (sh_pool_used < sh_stat_next)
		return;
	sh_stat_next = sh_pool_used + (8UL << 20);
	kc2d_serial_puts("\n[kc2d] pool ");
	kc2d_serial_hex(sh_pool_used);
	kc2d_serial_puts(" leaves:");
	for (k = 0; k < ST_N; k++) {
		kc2d_serial_puts(nm[k]);
		kc2d_serial_hex(sh_stat[k]);
	}
	kc2d_serial_puts(" vm ");
	kc2d_serial_hex(sh_vm_lo);
	kc2d_serial_puts("..");
	kc2d_serial_hex(sh_vm_hi);
	kc2d_serial_puts("\n[kc2d] direct: slab ");
	kc2d_serial_hex(sh_pg_slab);
	kc2d_serial_puts(" pgtable ");
	kc2d_serial_hex(sh_pg_table);
	kc2d_serial_puts(" other ");
	kc2d_serial_hex(sh_pg_other);
	kc2d_serial_puts("\n");
	for (k = 0; k < SH_NC; k++) {
		if (!sh_cache[k])
			break;
		kc2d_serial_puts("[kc2d]   ");
		kc2d_serial_puts(sh_cache_nm[k]);
		kc2d_serial_puts(" ");
		kc2d_serial_hex(sh_cache_n[k]);
		kc2d_serial_puts("\n");
	}
	sh_stat_proc_dump(16);
}

/* Entry for slot, creating tables as needed (interrupts off).  */
static tzd_sh_ent *sh_get(const void *slot)
{
	unsigned long a = sh_index_check(slot);
	void ***p1;
	void **p, **q;
	tzd_sh_ent *leaf;

	sh_ready();
	leaf = sh_cached(a);
	if (leaf)
		return &leaf[SH_IS(a)];
	p1 = &sh_top[SH_I1(a)];

	if (!*p1)
		*p1 = sh_alloc(PAGE_SIZE);
	p = &(*p1)[SH_I2(a)];
	if (!*p)
		*p = sh_alloc(PAGE_SIZE);
	q = &((void **)*p)[SH_I3(a)];
	if (!*q)
		*q = sh_alloc(PAGE_SIZE);
	p = &((void **)*q)[SH_I4(a)];
	if (!*p) {
		leaf = sh_alloc(SH_LEAF_BYTES);
		sh_leaf_pg[((char *)leaf - sh_pool) >> PAGE_SHIFT] = a >> PAGE_SHIFT;
		*p = leaf;
		sh_stat_leaf(a);
	}
	WRITE_ONCE(sh_lc[SH_LC_H(a >> PAGE_SHIFT)], *p);
	return &((tzd_sh_ent *)*p)[SH_IS(a)];
}

static tzd_ptr tzd_null(void)
{
	tzd_ptr p = { NULL, NULL, 0 };
	return p;
}

/* Bounds while running from the identity mapping (see the top).  */
static tzd_ptr kc2d_early(char *v)
{
	tzd_ptr p = { v, NULL, 0 };

	if (v) {
		p.base = (char *)PAGE_SIZE;
		p.size = (1UL << 47) - PAGE_SIZE;
	}
	return p;
}

tzd_ptr tzd_from_native(const void *a);

tzd_ptr tzd_sh_load(const void *slot)
{
	char *v = *(char *const *)slot;
	tzd_sh_ent *e;
	tzd_ptr p;

	if (kc2d_identity())
		return kc2d_early(v);
	sh_ready();
	e = sh_find(slot);
	if (e && e->live && e->addr == v) {
		p.addr = e->addr;
		p.base = e->base;
		p.size = e->size;
		return p;
	}
	if (!v)
		return tzd_null();
	/* No record of this value: the slot was written as an integer (lib/
	   sort.c swaps elements as u64, asm writes per-cpu pointers, a
	   struct copied as words).  As for any integer that becomes a
	   pointer in kernel mode: the bounds of the region it lies in, no
	   bounds outside every region (要確認, task #42).  */
	return tzd_from_native(v);
}

static void sh_record(const void *slot, tzd_ptr v)
{
	unsigned long flags;
	tzd_sh_ent *e;

	if (!v.addr && !v.base) {
		e = sh_find(slot);
		if (e)
			e->live = 0;
		return;
	}
	flags = arch_local_irq_save();
	e = sh_get(slot);
	e->addr = v.addr;
	e->base = v.base;
	e->size = v.size;
	e->live = 1;
	arch_local_irq_restore(flags);
}

void tzd_sh_store(void *slot, tzd_ptr v)
{
	sh_index_check(slot);
	*(char **)slot = v.addr;
	if (kc2d_identity())
		return;
	sh_ready();
	sh_record(slot, v);
}

void tzd_sh_record(const void *slot, tzd_ptr v)
{
	if ((unsigned long)slot & 7)
		return;
	if (kc2d_identity())
		return;
	sh_ready();
	sh_record(slot, v);
}

int tzd_sh_match(const void *slot)
{
	tzd_sh_ent *e;

	if ((unsigned long)slot & 7 || kc2d_identity())
		return 0;
	sh_ready();
	e = sh_find(slot);
	return e && e->live && e->addr == *(char *const *)slot;
}

void tzd_sh_clear(void *p, unsigned long n)
{
	unsigned long a, end;

	if (!n || kc2d_identity())
		return;
	a = (unsigned long)p & ~7UL;
	end = (unsigned long)p + n;
	while (a < end) {
		tzd_sh_ent *e = sh_find((void *)a);
		unsigned long stop = (a & PAGE_MASK) + PAGE_SIZE;

		/* One lookup per page: its entries are one array.  A page
		   with no leaf has nothing to clear.  */
		if (e) {
			if (stop > end)
				stop = end;
			for (; a < stop; a += 8, e++)
				e->live = 0;
		} else {
			a = stop;
		}
	}
}

/* Release: the bytes are overwritten once with a fixed meaningless value,
   the byte 0xa5 (TK 2026-11-17: the two stages used until then -- zero,
   then other bytes -- added nothing; as a word the value is not zero,
   not a small integer and not a canonical address), and the bounds
   entries are dropped.  Whole words where the range allows.  */
#define SH_WIPE_BYTE	0xa5
#define SH_WIPE_WORD	0xa5a5a5a5a5a5a5a5UL
static __always_inline void sh_wipe_bytes(void *p, unsigned long n)
{
	volatile unsigned char *b = p;
	unsigned long i;

	{
		/* A wipe stays inside its caller's frame: reaching a saved
		   frame pointer and return address on the frame chain means
		   a wrong address or size (debug check, task #42).  Frames
		   go up the stack, so the walk ends at the first one that
		   lies above the range.  */
		unsigned long *fp = __builtin_frame_address(0);
		int k;

		for (k = 0; k < 32 && fp; k++) {
			unsigned long *up;

			if ((char *)fp >= (char *)p + n)
				break;
			up = (unsigned long *)fp[0];
			if ((char *)p < (char *)(fp + 2)) {
				kc2d_serial_puts("\n[kc2d] wipe over a frame: p ");
				kc2d_serial_hex((unsigned long)p);
				kc2d_serial_puts(" n ");
				kc2d_serial_hex(n);
				kc2d_serial_puts(" frame ");
				kc2d_serial_hex((unsigned long)fp);
				kc2d_serial_puts(" ret ");
				kc2d_serial_hex(fp[1]);
				kc2d_fail("tzd: wipe reaches a saved return address\n");
			}
			if (up <= fp || (char *)up - (char *)fp > 65536)
				break;
			fp = up;
		}
	}
	if (!n)
		return;
	for (i = 0; i < n && (((unsigned long)p + i) & 7); i++)
		b[i] = SH_WIPE_BYTE;
	for (; i + 8 <= n; i += 8)
		*(volatile unsigned long *)((char *)p + i) = SH_WIPE_WORD;
	for (; i < n; i++)
		b[i] = SH_WIPE_BYTE;
	asm volatile("" : : "r"(p) : "memory");
}

void tzd_sh_wipe(void *p, unsigned long n)
{
	sh_wipe_bytes(p, n);
	tzd_sh_clear(p, n);
}

/* The bytes only: for memory that cannot have bounds entries (cc1tzd's
   late pass, a variable without pointers whose address is never taken).  */
void tzd_sh_wipe_data(void *p, unsigned long n)
{
	sh_wipe_bytes(p, n);
}

/* `#pragma tzd stack_reader` (task #72): the stack unwinder reads the
   stack word by word through addresses that have no bounds (the frame
   pointer chain starts at __builtin_frame_address, which cc1tzd makes
   unreadable).  Inside the named functions cc1tzd reads a word through
   tzd_stack_view (address, base, size):
   - inside the pointer's own bounds: the address itself;
   - outside them, 8-byte aligned and inside a kernel stack of the
     running task (its task stack, this CPU's hardirq stack, the
     exception stacks): the word is copied into a small ring and the
     copy's address returned -- a read sees the value, a store never
     reaches the stack;
   - an address in the ring itself (READ_ONCE takes the address again);
   - anything else: the bounds trap, as everywhere.
   Another task's stack is not accepted (no cheap way to tell that an
   address is one): printing all tasks' stacks stays a trap.  */
#define SH_VIEW_N	32
static unsigned long sh_view[SH_VIEW_N] __section(".data");
static unsigned int sh_view_i __section(".data");

static bool sh_on_stack(unsigned long a)
{
	unsigned long lo;

	if (a & 7)
		return false;
	lo = (unsigned long)task_stack_page(current);
	if (a - lo < THREAD_SIZE)
		return true;
	lo = (unsigned long)__this_cpu_read(pcpu_hot.hardirq_stack_ptr) + 8
	     - IRQ_STACK_SIZE;
	if (a - lo < IRQ_STACK_SIZE)
		return true;
	lo = (unsigned long)__this_cpu_read(cea_exception_stacks);
	if (lo && a - lo < sizeof(struct cea_exception_stacks))
		return true;
	return false;
}

void *tzd_stack_view(const void *a, const void *b, unsigned long s)
{
	unsigned long ua = (unsigned long)a, ub = (unsigned long)b;
	unsigned long *slot;

	if (b && s >= 8 && ua - ub <= s - 8)
		return (void *)a;
	if (!kc2d_identity()) {
		if (ua - (unsigned long)sh_view < sizeof sh_view && !(ua & 7))
			return (void *)a;
		if (sh_on_stack(ua)) {
			slot = &sh_view[sh_view_i++ % SH_VIEW_N];
			*slot = *(const unsigned long *)a;
			return slot;
		}
	}
	tzd_trap_bounds("stack_reader: tzd: out-of-bounds pointer access\n",
			a, b, s, 8);
}

void tzd_sh_copy(void *dst, const void *src, unsigned long n)
{
	unsigned long d = (unsigned long)dst, s = (unsigned long)src, i;
	unsigned long lead, cnt;

	if (!n || d == s || kc2d_identity())
		return;
	if ((d ^ s) & 7) {
		tzd_sh_clear(dst, n);
		return;
	}
	lead = (8 - (d & 7)) & 7;
	cnt = n > lead ? (n - lead) / 8 : 0;
	if (lead)
		tzd_sh_clear(dst, lead < n ? lead : n);
	if (n > lead + cnt * 8)
		tzd_sh_clear((char *)dst + lead + cnt * 8, n - lead - cnt * 8);
	d += lead;
	s += lead;
	for (i = 0; i < cnt; i++) {
		unsigned long k = d < s ? i : cnt - 1 - i;
		tzd_sh_ent *se = sh_find((void *)(s + k * 8));

		if (se && se->live) {
			unsigned long flags = arch_local_irq_save();

			*sh_get((void *)(d + k * 8)) = *se;
			arch_local_irq_restore(flags);
		} else {
			tzd_sh_clear((void *)(d + k * 8), 8);
		}
	}
}

/* Static initializers: every unit's tzd_shinit table lists {slot, base,
   size} for the pointers in its initialized data.  Registered on the
   first table access after the switch to the kernel mapping.  */
struct tzd_sh_init {
	char *slot;
	char *base;
	unsigned long size;
};
extern const struct tzd_sh_init __start_tzd_shinit[] __attribute__((weak));
extern const struct tzd_sh_init __stop_tzd_shinit[] __attribute__((weak));

void tzd_sh_init_static(void)
{
	const struct tzd_sh_init *t = __start_tzd_shinit;

	if (!t)
		return;
	for (; t < __stop_tzd_shinit; t++) {
		tzd_ptr v;

		if (!t->slot)
			continue;
		v.addr = *(char **)t->slot;
		v.base = t->base;
		v.size = t->size;
		if (v.size >> 63)
			v.size = *(const unsigned long *)(v.size - (1UL << 63));
		tzd_sh_record(t->slot, v);
	}
}

/* ------------------------------------------------------ raw addresses */

/* Bounds for raw address a (an integer, an asm output, a native function's
   pointer): the memory region it lies in, or none (any access traps).  Coarse on purpose for the first
   boot (whole regions); slab objects and folios come next (task #42).  */
tzd_ptr tzd_from_native(const void *a)
{
	unsigned long v = (unsigned long)a;
	unsigned long lo, hi;
	tzd_ptr p = { (char *)a, NULL, 0 };

	if (!v)
		return p;
	if (kc2d_identity())
		return kc2d_early((char *)a);
	if (v >= (unsigned long)_text && v < (unsigned long)_end) {
		lo = (unsigned long)_text;
		hi = (unsigned long)_end;
	} else if (v >= PAGE_OFFSET && v < PAGE_OFFSET + (max_pfn ? max_pfn << PAGE_SHIFT : 1UL << MAX_PHYSMEM_BITS)) {
		lo = PAGE_OFFSET;
		hi = PAGE_OFFSET + (max_pfn ? max_pfn << PAGE_SHIFT : 1UL << MAX_PHYSMEM_BITS);
	} else if (v >= VMALLOC_START && v <= VMALLOC_END) {
		lo = VMALLOC_START;
		hi = VMALLOC_END + 1;
	} else if (v >= VMEMMAP_START && v < VMEMMAP_START + (1UL << (MAX_PHYSMEM_BITS - PAGE_SHIFT)) * sizeof(struct page)) {
		lo = VMEMMAP_START;
		hi = VMEMMAP_START + (1UL << (MAX_PHYSMEM_BITS - PAGE_SHIFT)) * sizeof(struct page);
	} else if (v >= CPU_ENTRY_AREA_BASE && v < CPU_ENTRY_AREA_BASE + CPU_ENTRY_AREA_MAP_SIZE) {
		lo = CPU_ENTRY_AREA_BASE;
		hi = CPU_ENTRY_AREA_BASE + CPU_ENTRY_AREA_MAP_SIZE;
	} else if (v >= FIXADDR_TOT_START && v < FIXADDR_TOP) {
		/* Including the boot-time slots (early_memremap's FIX_BTMAP).  */
		lo = FIXADDR_TOT_START;
		hi = FIXADDR_TOP;
	} else if (v >= __START_KERNEL_map && v < (unsigned long)MODULES_END) {
		/* Kernel mapping outside the image (modules area).  */
		lo = __START_KERNEL_map;
		hi = MODULES_END;
	} else if (v < TASK_SIZE_MAX) {
		/* User address (__user): only reached through uaccess.  */
		lo = PAGE_SIZE;
		hi = TASK_SIZE_MAX;
		if (v < lo)
			return p;	/* near-null: no bounds */
	} else {
		/* In no region (ERR_PTR values, poison): no bounds, so it
		   traps when used, not when made.  */
		return p;
	}
	p.base = (char *)lo;
	p.size = hi - lo;
	return p;
}

/* ----------------------------------------------------- checked mem/str */

static __always_inline void kc2d_check(tzd_ptr p, unsigned long n)
{
	if (!n)
		return;
	if (!p.base)
		kc2d_fail("tzd: null pointer access (kc2d)\n");
	if (p.addr < p.base || (unsigned long)(p.addr - p.base) > p.size ||
	    n > p.size - (unsigned long)(p.addr - p.base)) {
		kc2d_serial_puts("\n[kc2d] addr ");
		kc2d_serial_hex((unsigned long)p.addr);
		kc2d_serial_puts(" base ");
		kc2d_serial_hex((unsigned long)p.base);
		kc2d_serial_puts(" size ");
		kc2d_serial_hex(p.size);
		kc2d_serial_puts(" n ");
		kc2d_serial_hex(n);
		kc2d_fail("tzd: out-of-bounds pointer access (kc2d)\n");
	}
}

/* Bytes from p.addr to the end of its bounds.  */
static __always_inline unsigned long kc2d_room(tzd_ptr p)
{
	if (!p.base)
		kc2d_fail("tzd: null pointer access (kc2d)\n");
	if (p.addr < p.base || (unsigned long)(p.addr - p.base) > p.size)
		kc2d_fail("tzd: out-of-bounds pointer access (kc2d)\n");
	return p.size - (unsigned long)(p.addr - p.base);
}

extern void *__memcpy(void *, const void *, size_t);
extern void *__memmove(void *, const void *, size_t);
extern void *__memset(void *, int, size_t);

tzd_ptr tzd_memcpy(tzd_ptr d, tzd_ptr s, unsigned long n)
{
	kc2d_check(d, n);
	kc2d_check(s, n);
	if (n) {
		__memcpy(d.addr, s.addr, n);
		tzd_sh_copy(d.addr, s.addr, n);
	}
	return d;
}

tzd_ptr tzd_memmove(tzd_ptr d, tzd_ptr s, unsigned long n)
{
	kc2d_check(d, n);
	kc2d_check(s, n);
	if (n) {
		tzd_sh_copy(d.addr, s.addr, n);
		__memmove(d.addr, s.addr, n);
	}
	return d;
}

tzd_ptr tzd_memset(tzd_ptr d, int c, unsigned long n)
{
	kc2d_check(d, n);
	if (n) {
		__memset(d.addr, c, n);
		tzd_sh_clear(d.addr, n);
	}
	return d;
}

int tzd_memcmp(tzd_ptr a, tzd_ptr b, unsigned long n)
{
	const unsigned char *x = (const unsigned char *)a.addr;
	const unsigned char *y = (const unsigned char *)b.addr;
	unsigned long i;

	kc2d_check(a, n);
	kc2d_check(b, n);
	for (i = 0; i < n; i++)
		if (x[i] != y[i])
			return x[i] < y[i] ? -1 : 1;
	return 0;
}

tzd_ptr tzd_memchr(tzd_ptr s, int c, unsigned long n)
{
	unsigned long i;

	kc2d_check(s, n);
	for (i = 0; i < n; i++)
		if ((unsigned char)s.addr[i] == (unsigned char)c) {
			s.addr += i;
			return s;
		}
	return tzd_null();
}

unsigned long tzd_strlen(tzd_ptr s)
{
	unsigned long room = kc2d_room(s), i;

	for (i = 0; i < room; i++)
		if (!s.addr[i])
			return i;
	kc2d_fail("tzd: out-of-bounds pointer access (kc2d strlen)\n");
}

int tzd_strcmp(tzd_ptr a, tzd_ptr b)
{
	unsigned long ra = kc2d_room(a), rb = kc2d_room(b), i;

	for (i = 0; i < ra && i < rb; i++) {
		unsigned char x = a.addr[i], y = b.addr[i];

		if (x != y)
			return x < y ? -1 : 1;
		if (!x)
			return 0;
	}
	kc2d_fail("tzd: out-of-bounds pointer access (kc2d strcmp)\n");
}

int tzd_strncmp(tzd_ptr a, tzd_ptr b, unsigned long n)
{
	unsigned long ra = kc2d_room(a), rb = kc2d_room(b), i;

	for (i = 0; i < n; i++) {
		unsigned char x, y;

		if (i >= ra || i >= rb)
			kc2d_fail("tzd: out-of-bounds pointer access (kc2d strncmp)\n");
		x = a.addr[i];
		y = b.addr[i];
		if (x != y)
			return x < y ? -1 : 1;
		if (!x)
			return 0;
	}
	return 0;
}

#define KC2D_CTYPE(name, expr) \
	int tzd_##name(int c) { return (c >= 0 && c <= 255) ? (expr) : 0; }
KC2D_CTYPE(isdigit, c >= '0' && c <= '9')
KC2D_CTYPE(isxdigit, (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f'))
KC2D_CTYPE(isspace, c == ' ' || (c >= '\t' && c <= '\r'))
KC2D_CTYPE(isupper, c >= 'A' && c <= 'Z')
KC2D_CTYPE(islower, c >= 'a' && c <= 'z')
KC2D_CTYPE(isalpha, (c | 32) >= 'a' && (c | 32) <= 'z')
KC2D_CTYPE(isalnum, ((c | 32) >= 'a' && (c | 32) <= 'z') || (c >= '0' && c <= '9'))
KC2D_CTYPE(isprint, c >= 0x20 && c < 0x7f)
int tzd_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int tzd_toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* ----------------------------------- function-pointer interface (task #61)

   cc1tzd emits tzd_fn_reg where a function is erased to the generic
   void(*)(void) and tzd_fn_check at __tzd_fncast.  Kernel code is not
   annotated with __tzd_fncast, so tzd_fn_check is effectively never reached
   in the kernel; tzd_fn_reg only has to record without crashing.  A fixed
   open-addressing table (no allocator here); staying below 75% load keeps
   probing bounded, and overflow panics (fail-stop).  The table is .bss, so
   clear_bss zeroes it during early boot -- harmless, as nothing checks an
   entry before then.  */
#define FN_N (1u << 15)
static const void *fn_key[FN_N];
static unsigned long fn_id[FN_N];
static unsigned fn_cnt;

static unsigned fn_hash(const void *fn)
{
	unsigned long x = (unsigned long) fn;
	x ^= x >> 33; x *= 0xff51afd7ed558ccdUL;
	x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53UL;
	x ^= x >> 33;
	return (unsigned) x & (FN_N - 1);
}

void tzd_fn_reg(const void *fn, unsigned long id)
{
	unsigned i;
	if (!fn)
		return;
	i = fn_hash(fn);
	while (fn_key[i]) {
		if (fn_key[i] == fn) {
			if (fn_id[i] != id)
				tzd_trap("kc2d: a function is registered under two "
					 "interfaces (task #61)\n");
			return;
		}
		i = (i + 1) & (FN_N - 1);
	}
	if (fn_cnt * 4 >= FN_N * 3)
		tzd_trap("kc2d: function interface table full (task #61)\n");
	fn_key[i] = fn;
	fn_id[i] = id;
	fn_cnt++;
}

void tzd_fn_check(const void *fn, unsigned long id)
{
	unsigned i;
	if (!fn)
		return;
	for (i = fn_hash(fn); fn_key[i]; i = (i + 1) & (FN_N - 1))
		if (fn_key[i] == fn) {
			if (fn_id[i] == id)
				return;
			tzd_trap("kc2d: function pointer interface mismatch "
				 "(task #61)\n");
		}
	tzd_trap("kc2d: function pointer not registered for this interface "
		 "(task #61)\n");
}
