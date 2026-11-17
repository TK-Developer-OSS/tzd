/* ctype and string-to-number calls from host headers go to libc2d's
   Managed C versions (task #43): glibc's ctype macros are off
   (__NO_CTYPE), strtol/strtoul set endptr with the string's bounds and
   errno on overflow.  */
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *end;
    const char *s = "  -0x1fz";
    if (!isalpha('a') || isalpha('1') || !isdigit('7') || !isspace('\t')
        || !isxdigit('F') || !isblank(' ') || ispunct('a') || !ispunct('!')
        || tolower('Q') != 'q' || toupper('q') != 'Q' || isalpha(-1))
        return 1;
    if (strtol(s, &end, 16) != -31 || *end != 'z')
        return 2;
    if (strtol(s, &end, 0) != -31 || strcmp(end, "z") != 0)
        return 3;
    if (strtol("0755", 0, 0) != 493 || strtol("zz", &end, 36) != 1295)
        return 4;
    if (strtol("abc", &end, 10) != 0 || end[0] != 'a')
        return 5;
    errno = 0;
    if (strtol("99999999999999999999", 0, 10) != 9223372036854775807L
        || errno != ERANGE)
        return 6;
    if (strtoul("-1", 0, 10) != (unsigned long) -1 || strtoul("42", 0, 8) != 34)
        return 7;
    if (atoi(" -12x") != -12 || atol("123456789012") != 123456789012L
        || abs(-5) != 5 || labs(-7L) != 7)
        return 8;
    return 0;
}
