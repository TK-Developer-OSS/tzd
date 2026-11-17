// SPDX-License-Identifier: MIT
/* ctype, C locale (ASCII), Managed C.  Written from the C standard's
   definitions.

   The argument must be EOF or a value of unsigned char, as in ISO C.  A
   plain char holding a negative byte is the classic out-of-range index
   into a host ctype table; here it traps instead.  */
#include <c2d_rt.h>

int tzd_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int tzd_toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

static int ct_arg(int c)
{
    if (c < -1 || c > 255)
        tzd_trap("libc2d: ctype argument is not EOF or an unsigned char value\n");
    return c;
}

int tzd_isdigit(int c)  { c = ct_arg(c); return c >= '0' && c <= '9'; }
int tzd_isupper(int c)  { c = ct_arg(c); return c >= 'A' && c <= 'Z'; }
int tzd_islower(int c)  { c = ct_arg(c); return c >= 'a' && c <= 'z'; }
int tzd_isalpha(int c)  { return tzd_isupper(c) || tzd_islower(c); }
int tzd_isalnum(int c)  { return tzd_isalpha(c) || tzd_isdigit(c); }
int tzd_isxdigit(int c)
{
    c = ct_arg(c);
    return tzd_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int tzd_isspace(int c)  { c = ct_arg(c); return c == ' ' || (c >= '\t' && c <= '\r'); }
int tzd_iscntrl(int c)  { c = ct_arg(c); return (c >= 0 && c < 32) || c == 127; }
int tzd_isprint(int c)  { c = ct_arg(c); return c >= 32 && c < 127; }
int tzd_isgraph(int c)  { c = ct_arg(c); return c > 32 && c < 127; }
int tzd_ispunct(int c)  { return tzd_isgraph(c) && !tzd_isalnum(c); }
int tzd_isblank(int c)  { c = ct_arg(c); return c == ' ' || c == '\t'; }
