/* strtol/strtoul, Managed C (written for libc2d).  Separate from
   stdlib.c because they set errno, which lives in the host libc
   (--nolibc programs have none).  */
#include <c2d_rt.h>

/* Digit value in base 36, or 99.  */
static unsigned digit_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 99;
}

/* Common part of strtol/strtoul (ISO C 7.22.1.4).  Parses into *mag with
   saturation at lim; returns the index after the number, or 0 when there
   is no number (endptr then gets the start of the string).  */
static size_t parse_num(const char *s, int base, unsigned long lim_pos,
                        unsigned long lim_neg, unsigned long *mag, int *neg,
                        int *over)
{
    size_t i = 0, start;
    unsigned long v = 0, lim;
    *neg = 0;
    *over = 0;
    if (base < 0 || base == 1 || base > 36) {
        *__errno_location() = C2D_EINVAL;
        *mag = 0;
        return 0;
    }
    while (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))
        i++;
    if (s[i] == '-' || s[i] == '+') {
        *neg = s[i] == '-';
        i++;
    }
    if ((base == 0 || base == 16) && s[i] == '0'
        && (s[i + 1] == 'x' || s[i + 1] == 'X') && digit_value(s[i + 2]) < 16) {
        i += 2;
        base = 16;
    } else if (base == 0)
        base = s[i] == '0' ? 8 : 10;
    lim = *neg ? lim_neg : lim_pos;
    start = i;
    for (;; i++) {
        unsigned d = digit_value(s[i]);
        if (d >= (unsigned) base)
            break;
        if (*over)
            continue;
        if (v > (lim - d) / (unsigned) base) {
            *over = 1;
            v = lim;
        } else
            v = v * base + d;
    }
    *mag = v;
    if (i == start) {
        *mag = 0;
        return 0;
    }
    if (*over)
        *__errno_location() = C2D_ERANGE;
    return i;
}

long tzd_strtol(const char *s, char **endptr, int base)
{
    unsigned long mag;
    int neg, over;
    size_t end = parse_num(s, base, 9223372036854775807UL,
                           9223372036854775808UL, &mag, &neg, &over);
    if (endptr)
        *endptr = (char *) s + end;
    return neg ? (long) (0 - mag) : (long) mag;
}

unsigned long tzd_strtoul(const char *s, char **endptr, int base)
{
    unsigned long mag;
    int neg, over;
    /* A minus sign negates in unsigned arithmetic (ISO C); overflow is
       judged on the magnitude.  */
    size_t end = parse_num(s, base, (unsigned long) -1, (unsigned long) -1,
                           &mag, &neg, &over);
    if (endptr)
        *endptr = (char *) s + end;
    if (over)
        return (unsigned long) -1;
    return neg ? 0 - mag : mag;
}
