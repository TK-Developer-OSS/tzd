#include <d/pointer.h>
#include "sys.h"

void tzd_trap(const char *msg)
{
    tzd_size_t n = 0;
    while (msg[n])
        n++;
    tzd_sys_write(2, tzd_ptr_make((void *) msg, n), n);
    tzd_sys_exit(134);
}

/* ---- the dump of an out-of-bounds access (tzd_trap_bounds) ---- */

static void dump_str(const char *s)
{
    tzd_size_t n = 0;
    while (s[n])
        n++;
    tzd_sys_write(2, tzd_ptr_make((void *) s, n), n);
}

static void dump_hex(unsigned long v, int digits)
{
    char b[19];
    int i;
    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < digits; i++)
        b[2 + i] = "0123456789abcdef"[(v >> (4 * (digits - 1 - i))) & 15];
    b[2 + digits] = 0;
    dump_str(b);
}

static void dump_dec(unsigned long v)
{
    char b[24];
    int i = 23;
    b[i] = 0;
    do {
        b[--i] = (char) ('0' + v % 10);
        v /= 10;
    } while (v);
    dump_str(&b[i]);
}

/* An access of N bytes at ADDR outside the bounds [BASE, BASE + SIZE):
   MSG (with the source position), then a short dump of where it happened:
   the access, the object and how far off it is, the object's first bytes
   (only bytes inside it are read), and the callers (frame pointers;
   addr2line -e <program> gives the lines).  Exit status 134 as for any
   trap.  */
static void trap_dump(const char *msg, const void *addr, const void *base,
                      tzd_size_t size, tzd_size_t n, int bytes)
    __attribute__((noreturn));
static void dump_calls(unsigned long *fp);

void tzd_trap_bounds(const char *msg, const void *addr, const void *base,
                     tzd_size_t size, tzd_size_t n)
{
    trap_dump(msg, addr, base, size, n, 1);
}

/* As tzd_trap_bounds, without the object's bytes (it may be gone: a
   double free).  */
void tzd_trap_ptr(const char *msg, const void *addr, const void *base,
                  tzd_size_t size)
{
    trap_dump(msg, addr, base, size, 0, 0);
}

static void trap_dump(const char *msg, const void *addr, const void *base,
                      tzd_size_t size, tzd_size_t n, int bytes)
{
    unsigned long a = (unsigned long) addr, b = (unsigned long) base;
    dump_str(msg);
    dump_str("  access : ");
    dump_hex(a, 16);
    dump_str(", ");
    dump_dec(n);
    dump_str(" bytes\n");
    if (!base) {
        dump_str("  object : none (the pointer has no bounds)\n");
    } else {
        dump_str("  object : ");
        dump_hex(b, 16);
        dump_str(" .. ");
        dump_hex(b + size, 16);
        dump_str(" (");
        dump_dec(size);
        dump_str(" bytes)\n  where  : ");
        if (a < b) {
            dump_dec(b - a);
            dump_str(" bytes before the object\n");
        } else if (a - b >= size) {
            dump_dec(a - b - size);
            dump_str(" bytes past the end of the object\n");
        } else if (a + n > b + size) {
            dump_str("offset ");
            dump_dec(a - b);
            dump_str(" in the object; the access runs ");
            dump_dec(a + n - (b + size));
            dump_str(" bytes past its end\n");
        } else {
            dump_str("offset ");
            dump_dec(a - b);
            dump_str(" in the object\n");
        }
        if (bytes && !tzd_sys_mapped(base, size < 64 ? size : 64))
            dump_str("  bytes  : (not mapped: the bounds are stale)\n");
        else if (bytes) {
            const unsigned char *o = (const unsigned char *) base;
            tzd_size_t i, m = size < 64 ? size : 64;
            for (i = 0; i < m; i++) {
                if (i % 16 == 0) {
                    dump_str(i ? "\n           +" : "  bytes  : +");
                    dump_hex(i, 2);
                    dump_str(":");
                }
                dump_str(" ");
                dump_hex(o[i], 2);
            }
            if (m)
                dump_str(size > m ? " ...\n" : "\n");
        }
    }
    dump_calls((unsigned long *) __builtin_frame_address(0));
    tzd_sys_exit(134);
}

/* The callers, through the frame pointers (TzD code keeps them).  */
static void dump_calls(unsigned long *fp)
{
    {
        int k;
        dump_str("  calls  :");
        for (k = 0; k < 8 && fp; k++) {
            unsigned long *up = (unsigned long *) fp[0];
            if (up <= fp || (unsigned long) up - (unsigned long) fp > (1UL << 20))
                break;
            /* Past the frames that keep a frame pointer (the C library's
               start code): the "return address" is then a stack address
               or data.  */
            if (up[1] - (unsigned long) fp < (1UL << 24)
                || (unsigned long) fp - up[1] < (1UL << 24) || up[1] < 0x1000)
                break;
            dump_str(" ");
            dump_hex(up[1], 12);
            fp = up;
        }
        dump_str("   (addr2line -f -e <program> <address>)\n");
    }
}

/* A pointer read from memory (SLOT) that holds VALUE but has no bounds
   recorded for it (a pointer written there by host code, a memcpy of
   pointer-holding bytes through a non-pointer type, ...): MSG, the slot,
   the value, the callers.  */
void tzd_trap_slot(const char *msg, const void *slot, const void *value)
{
    dump_str(msg);
    dump_str("  slot   : ");
    dump_hex((unsigned long) slot, 16);
    dump_str("\n  value  : ");
    dump_hex((unsigned long) value, 16);
    dump_str("\n");
    dump_calls((unsigned long *) __builtin_frame_address(0));
    tzd_sys_exit(134);
}

tzd_ptr tzd_ptr_null(void)
{
    tzd_ptr p = { 0, 0, 0 };
    return p;
}

tzd_ptr tzd_ptr_make(void *base, tzd_size_t size)
{
    tzd_ptr p;
    p.addr = base;
    p.base = base;
    p.size = size;
    return p;
}

tzd_ptr tzd_ptr_add(tzd_ptr p, tzd_ptrdiff_t n)
{
    p.addr += n;
    return p;
}

tzd_ptrdiff_t tzd_ptr_diff(tzd_ptr a, tzd_ptr b)
{
    if (a.base != b.base)
        tzd_trap("libc2d: pointer difference across allocations\n");
    return a.addr - b.addr;
}

int tzd_ptr_valid(tzd_ptr p, tzd_size_t n)
{
    tzd_size_t off;
    if (p.base == 0 || p.addr < p.base)
        return 0;
    off = (tzd_size_t) (p.addr - p.base);
    return off <= p.size && n <= p.size - off;
}

void tzd_ptr_check(tzd_ptr p, tzd_size_t n)
{
    if (p.base == 0)
        tzd_trap("libc2d: null pointer access\n");
    if (!tzd_ptr_valid(p, n))
        tzd_trap_bounds("libc2d: out-of-bounds access\n", p.addr, p.base,
                        p.size, n);
}

unsigned char tzd_ptr_load8(tzd_ptr p)
{
    tzd_ptr_check(p, 1);
    return (unsigned char) *p.addr;
}

void tzd_ptr_store8(tzd_ptr p, unsigned char v)
{
    tzd_ptr_check(p, 1);
    *p.addr = (char) v;
}

void *tzd_ptr_pointer(tzd_ptr p, tzd_size_t n)
{
    tzd_ptr_check(p, n);
    return p.addr;
}
