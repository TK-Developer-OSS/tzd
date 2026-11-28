/* libc2d: write groups (task #79, spec 5/6; user space first).

   A static object declared with `// @tzd group=G writer=F1,F2` lives in
   section tzd_grp; cc1tzd lists it in section tzd_grpd as
   {address, "G:F1,F2", size}.  Every store through a pointer (and every
   pointer argument a library function may write through) whose address
   falls in tzd_grp calls tzd_grp_store with the name of the function that
   writes: a function not listed for the object's group traps.  The
   section and its descriptor table are fixed at link time: there is
   nothing to initialize, so no write before main is unchecked.  */
#include <d/pointer.h>
#include "sys.h"

struct tzd_grp_desc {
    const char *addr;
    const char *spec;        /* "GROUP:WRITER,WRITER" */
    tzd_size_t size;
};

/* Keeps section tzd_grp (and so __start_tzd_grp / __stop_tzd_grp, which
   every unit's checks refer to) present in every program.  Never written:
   it has no descriptor, a store into it traps.  */
__attribute__((section("tzd_grp"), used, aligned(16)))
char tzd_grp_anchor[16];

extern const struct tzd_grp_desc __start_tzd_grpd[] __attribute__((weak));
extern const struct tzd_grp_desc __stop_tzd_grpd[] __attribute__((weak));

static void put(const char *s, tzd_size_t n)
{
    tzd_sys_write(2, tzd_ptr_make((void *) s, n), n);
}

static void puts0(const char *s)
{
    tzd_size_t n = 0;
    while (s[n])
        n++;
    put(s, n);
}

__attribute__((noreturn))
static void grp_trap(const char *what, const char *fn,
                     const struct tzd_grp_desc *d)
{
    puts0("tzd: ");
    puts0(what);
    if (d) {
        const char *s = d->spec;
        tzd_size_t n = 0;
        while (s[n] && s[n] != ':')
            n++;
        puts0(" of group ");
        put(s, n);
        puts0(" by ");
        puts0(fn);
        if (s[n] == ':' && s[n + 1]) {
            puts0(", which is not one of its writers (");
            puts0(s + n + 1);
            puts0(")");
        } else
            puts0(", which has no writers");
    } else {
        puts0(" by ");
        puts0(fn);
    }
    puts0(" (task #79)\n");
    tzd_sys_exit(134);
}

/* Is FN one of the comma-separated names after the ':' of SPEC?  */
static int listed(const char *spec, const char *fn)
{
    const char *s = spec;
    while (*s && *s != ':')
        s++;
    if (*s != ':')
        return 0;
    s++;
    while (*s) {
        tzd_size_t i = 0;
        while (s[i] && s[i] != ',' && fn[i] && s[i] == fn[i])
            i++;
        if ((s[i] == ',' || s[i] == 0) && fn[i] == 0)
            return 1;
        while (*s && *s != ',')
            s++;
        if (*s == ',')
            s++;
    }
    return 0;
}

void tzd_grp_store(const void *addr, const char *fn)
{
    const char *a = (const char *) addr;
    const struct tzd_grp_desc *d;
    for (d = __start_tzd_grpd; d < __stop_tzd_grpd; d++) {
        if (!d->addr || a < d->addr || a - d->addr >= (long) d->size)
            continue;
        if (listed(d->spec, fn))
            return;
        grp_trap("write to memory", fn, d);
    }
    grp_trap("write into the group section outside any grouped object",
             fn, 0);
}
