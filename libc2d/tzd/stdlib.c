// SPDX-License-Identifier: MIT
/* <stdlib.h> parts of libc2d written in Managed C.  The allocator core
   (malloc/calloc/realloc/free) and exit stay in libc2d/src: they create
   and release bounds.  */
#include <c2d_rt.h>

int tzd_abs(int n)
{
    if (n == -2147483647 - 1)
        tzd_trap("libc2d: abs of the most negative int\n");
    return n < 0 ? -n : n;
}

/* atoi: optional spaces, sign, decimal digits.  An unterminated string
   traps at the first byte past it.  Overflow traps instead of being
   undefined.  */
int tzd_atoi(const char *s)
{
    size_t i = 0;
    int neg = 0;
    long v = 0;
    while (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))
        i++;
    if (s[i] == '-' || s[i] == '+') {
        neg = s[i] == '-';
        i++;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        if (v > 2147483648L || (!neg && v > 2147483647L))
            tzd_trap("libc2d: atoi result does not fit in int\n");
        i++;
    }
    return (int) (neg ? -v : v);
}

long tzd_labs(long n)
{
    if (n == -9223372036854775807L - 1)
        tzd_trap("libc2d: labs of the most negative long\n");
    return n < 0 ? -n : n;
}

/* atol: as atoi, for long.  */
long tzd_atol(const char *s)
{
    size_t i = 0;
    int neg = 0;
    unsigned long v = 0, lim;
    while (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))
        i++;
    if (s[i] == '-' || s[i] == '+') {
        neg = s[i] == '-';
        i++;
    }
    lim = neg ? 9223372036854775808UL : 9223372036854775807UL;
    while (s[i] >= '0' && s[i] <= '9') {
        unsigned d = s[i] - '0';
        if (v > (lim - d) / 10)
            tzd_trap("libc2d: atol result does not fit in long\n");
        v = v * 10 + d;
        i++;
    }
    return neg ? (long) (0 - v) : (long) v;
}
