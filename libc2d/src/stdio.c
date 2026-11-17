// SPDX-License-Identifier: MIT
#include <d/stdio.h>
#include <d/string.h>
#include "sys.h"

static void out(tzd_ptr p, tzd_size_t n)
{
    if (n)
        tzd_sys_write(1, p, n);
}

int tzd_putchar(int c)
{
    char ch = (char) c;
    out(tzd_ptr_make(&ch, 1), 1);
    return (unsigned char) c;
}

int tzd_puts(tzd_ptr s)
{
    tzd_size_t n = tzd_strlen(s);
    out(s, n);
    tzd_putchar('\n');
    return (int) n + 1;
}

static int put_num(unsigned long v, unsigned base, int neg)
{
    char tmp[24];
    int i = 0, n;
    do {
        tmp[i++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    if (neg)
        tmp[i++] = '-';
    n = i;
    while (i)
        tzd_putchar(tmp[--i]);
    return n;
}

int tzd_printf(tzd_ptr fmt, ...)
{
    __builtin_va_list ap;
    tzd_size_t i = 0, len = tzd_strlen(fmt);
    int total = 0;

    __builtin_va_start(ap, fmt);
    while (i < len) {
        char c = (char) tzd_ptr_load8(tzd_ptr_add(fmt, (tzd_ptrdiff_t) i++));
        if (c != '%') {
            tzd_putchar(c);
            total++;
            continue;
        }
        if (i >= len)
            break;
        c = (char) tzd_ptr_load8(tzd_ptr_add(fmt, (tzd_ptrdiff_t) i++));
        switch (c) {
        case 'd': {
            int v = __builtin_va_arg(ap, int);
            total += v < 0 ? put_num(-(unsigned long) (long) v, 10, 1)
                           : put_num((unsigned long) v, 10, 0);
            break;
        }
        case 'u': total += put_num(__builtin_va_arg(ap, unsigned), 10, 0); break;
        case 'x': total += put_num(__builtin_va_arg(ap, unsigned), 16, 0); break;
        case 'c': tzd_putchar(__builtin_va_arg(ap, int)); total++; break;
        case 's': {
            tzd_ptr s = __builtin_va_arg(ap, tzd_ptr);
            tzd_size_t n = tzd_strlen(s);
            out(s, n);
            total += (int) n;
            break;
        }
        case '%': tzd_putchar('%'); total++; break;
        default:  tzd_putchar('%'); tzd_putchar(c); total += 2; break;
        }
    }
    __builtin_va_end(ap);
    return total;
}
