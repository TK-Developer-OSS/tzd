// SPDX-License-Identifier: MIT
/* Raw-C boundary helpers emitted by cc1tzd (provisional, task #14/#23).

   A Managed C `T **` passed to a host-libc parameter of type `T **`
   (getopt's argv, execv's argv, strtol's endptr) points at an array of
   8-byte pointer slots, which is already what the host expects (task #1
   (b)).  The call site does

       raw = tzd_pp_in(p);  r = f(..., raw, ...);  tzd_pp_out(p, raw);

   tzd_pp_in checks p and passes its address.  The host may store raw
   pointers into the slots; their bounds entries are then stale, so
   tzd_pp_out re-registers every slot whose value changed: a value that was
   one of the slots' own pointers before the call (getopt permutes argv)
   keeps that pointer's bounds; anything else comes from the host and is
   bounded below only (task #9).  Unchanged slots keep their bounds.  */
#include <d/pointer.h>
#include <d/stdlib.h>
#include <d/shadow.h>

#define D_PTRDIFF_MAX ((tzd_size_t) (~(tzd_size_t) 0 >> 1))

static tzd_size_t pp_count(tzd_ptr p)
{
    if (p.addr == 0 || p.base == 0 || p.addr < p.base)
        return 0;
    tzd_size_t off = (tzd_size_t) (p.addr - p.base);
    if (off >= p.size)
        return 0;
    return (p.size - off) / sizeof(char *);
}

void **tzd_pp_in(tzd_ptr p)
{
    if (p.addr == 0)
        return 0;
    tzd_size_t n = pp_count(p);
    if (n == 0)
        tzd_trap("libc2d: pointer array passed to the host is out of bounds\n");
    return (void **) tzd_ptr_pointer(p, n * sizeof(char *));
}

void tzd_pp_out(tzd_ptr p, void **raw)
{
    if (raw == 0)
        return;
    tzd_size_t n = pp_count(p), i, j;
    char **slot = (char **) raw;
    tzd_ptr om = tzd_malloc(n * sizeof(tzd_ptr) + 1);
    tzd_ptr *orig = (tzd_ptr *) tzd_ptr_pointer(om, n * sizeof(tzd_ptr) + 1);
    /* Snapshot the entries first: fixing slot i must not change what
       slot j had.  The host does not touch the entries, only the slots.  */
    for (i = 0; i < n; i++)
        if (!tzd_sh_peek(&slot[i], &orig[i]))
            orig[i] = tzd_ptr_null();
    for (i = 0; i < n; i++) {
        if (orig[i].base != 0 && slot[i] == orig[i].addr)
            continue;                          /* unchanged */
        if (slot[i] == 0) {
            tzd_sh_record(&slot[i], tzd_ptr_null());
            continue;
        }
        for (j = 0; j < n; j++)
            if (orig[j].base != 0 && orig[j].addr == slot[i])
                break;
        if (j < n)
            tzd_sh_record(&slot[i], orig[j]);     /* one of ours, moved */
        else {
            tzd_ptr h;                           /* from the host */
            h.addr = slot[i];
            h.base = slot[i];
            h.size = D_PTRDIFF_MAX;
            tzd_sh_record(&slot[i], h);
        }
    }
    tzd_free(om);
}
