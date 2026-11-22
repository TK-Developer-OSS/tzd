/* Function-pointer interface registry (task #61, resolving #22).

   A function pointer erased to the generic void(*)(void) of a dispatch
   table (OpenSSL's OSSL_DISPATCH) carries no type at run time, so a cast
   back to a typed pointer cannot be checked by the compiler.  cc1tzd emits

     tzd_fn_reg(fn, id)     where a named function is erased to the generic
                            type: record fn's real interface id; and
     tzd_fn_check(fn, id)   at __tzd_fncast(T, fn): fn must have been
                            registered with id == id(T), else trap.

   id is a hash of the signature, computed identically by cc1tzd at every
   site (same function type -> same id).  Registration is ground truth: it
   happens only for a named function, whose declared signature is known, so
   the table never holds a forged interface.  A generic pointer that was
   never registered (host origin, bytes overwritten, wrong dispatch id) is
   not found and traps -- bounds-style fail-stop, no guessing (security
   first).  Keyed by the function address; open addressing, mmap-backed,
   doubled and rehashed when it fills.  (SMP locking: task #49.)  */
#include <d/pointer.h>
#include "sys.h"

void tzd_trap(const char *msg) __attribute__((noreturn));

typedef struct { const void *fn; unsigned long id; } fn_ent;

static fn_ent *fn_tab;
static tzd_size_t fn_cap;          /* power of two, 0 until first use */
static tzd_size_t fn_cnt;

/* Mix the address so aligned function entry points spread across the table. */
static tzd_size_t fn_hash(const void *fn)
{
    tzd_size_t x = (tzd_size_t) fn;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdUL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53UL;
    x ^= x >> 33;
    return x;
}

/* The entry for fn, or the first empty slot on its probe chain. */
static fn_ent *fn_slot(const void *fn)
{
    tzd_size_t mask = fn_cap - 1;
    tzd_size_t i = fn_hash(fn) & mask;
    for (;;) {
        if (!fn_tab[i].fn || fn_tab[i].fn == fn)
            return &fn_tab[i];
        i = (i + 1) & mask;
    }
}

static void fn_grow(void)
{
    tzd_size_t ocap = fn_cap, i;
    fn_ent *old = fn_tab;
    tzd_size_t ncap = ocap ? ocap * 2 : 1024;
    fn_ent *nt = (fn_ent *) tzd_sys_mmap(ncap * sizeof *nt);  /* zero-filled */
    if (!nt)
        tzd_trap("libc2d: cannot map the function interface table (task #61)\n");
    fn_tab = nt; fn_cap = ncap; fn_cnt = 0;
    for (i = 0; i < ocap; i++)
        if (old[i].fn) {
            fn_ent *e = fn_slot(old[i].fn);
            e->fn = old[i].fn; e->id = old[i].id; fn_cnt++;
        }
    if (old)
        tzd_sys_munmap(old, ocap * sizeof *nt);
}

/* Record that the named function fn has interface id (ground truth). */
void tzd_fn_reg(const void *fn, unsigned long id)
{
    fn_ent *e;
    if (!fn)
        return;
    if (!fn_tab || (fn_cnt + 1) * 4 >= fn_cap * 3)    /* keep load < 0.75 */
        fn_grow();
    e = fn_slot(fn);
    if (!e->fn) { e->fn = fn; e->id = id; fn_cnt++; }
    else if (e->id != id)
        tzd_trap("libc2d: a function is registered under two interfaces "
                 "(task #61)\n");
}

/* At __tzd_fncast(T, fn): fn must have been registered with id(T). */
void tzd_fn_check(const void *fn, unsigned long id)
{
    if (!fn)
        return;                    /* a null function pointer is not confusion */
    if (fn_tab) {
        fn_ent *e = fn_slot(fn);
        if (e->fn == fn) {
            if (e->id == id)
                return;
            tzd_trap("libc2d: function pointer interface mismatch "
                     "(task #61)\n");
        }
    }
    tzd_trap("libc2d: function pointer not registered for this interface "
             "(task #61)\n");
}

/* Static dispatch tables: each unit's tzd_fninit section lists {fn, 0, id}
   to register at startup (a tzd_fn_reg call is not a constant, task #61).
   Same 24-byte record layout as tzd_shinit; zero (padding) entries are
   skipped, exactly as tzd_sh_init_static does.  */
struct tzd_fn_init { const void *fn; const void *pad; unsigned long id; };
extern const struct tzd_fn_init __start_tzd_fninit[] __attribute__((weak));
extern const struct tzd_fn_init __stop_tzd_fninit[] __attribute__((weak));

void tzd_fn_init_static(void)
{
    const struct tzd_fn_init *t = __start_tzd_fninit;
    if (!t)
        return;
    for (; t < __stop_tzd_fninit; t++)
        if (t->fn)
            tzd_fn_reg(t->fn, t->id);
}
